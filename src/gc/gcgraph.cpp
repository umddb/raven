#include <algorithm>
#include <endian.h>
#include <stack>

#include "boost/asio/post.hpp"
#include "rocksdb/db.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/options.h"
#include "rocksdb/table.h"
#include "gc/gcgraph.h"
#include "utils/config.h"


namespace ercat {

void GCGraph::init() {
    getInstanceImpl();
}

GCGraph& GCGraph::getInstance() {
    return getInstanceImpl();
}

GCGraph::~GCGraph() {
    shutDown();
}

std::vector<std::vector<ExtVertex>> GCGraph::updateGraphStore(std::vector<std::pair<ExtEdge, int>>& add_edges,
            std::vector<std::pair<ExtEdge, int>>& delete_edges, EpochId epoch_id, const std::string& delete_time) {
    std::mutex mtx;
    std::condition_variable cv;
    const int num_workers = num_rc_workers_;

    // update graph store with add edges and delete edges, identify vertices that are missing & return
    std::vector<std::vector<ExtVertex>> new_ext_vertices;
    for (int i = 0; i < num_workers; i++) {
        boost::asio::post(rc_workers_->workers_, [this, &add_edges, &delete_edges, epoch_id, &delete_time, 
                i, num_workers, &new_ext_vertices, &mtx, &cv](){ 
            updateGraphStoreImpl(add_edges, delete_edges, epoch_id, delete_time, i, num_workers, new_ext_vertices, mtx, cv);
        });
    }

    std::unique_lock<std::mutex>lock(mtx);
    cv.wait(lock, [&new_ext_vertices, num_workers](){ return new_ext_vertices.size() >= num_workers; });
    lock.unlock();

    return new_ext_vertices;
}

std::vector<Vertex> GCGraph::updateVertexMaps(std::vector<ExtVertex>& new_ext_vertices) {
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<int> rc_counter;
    const int num_workers = num_rc_workers_;

    // allocate the vertex ids
    std::vector<Vertex> new_vertices = allocateVertices(new_ext_vertices.size());

    // update the vertex map
    rc_counter.store(0);
    for (int i = 0; i < num_workers; i++) {
        boost::asio::post(rc_workers_->workers_, [this, &new_ext_vertices, &new_vertices, 
                i, num_workers, &rc_counter, &mtx, &cv](){ 
            updateVertexMapsImpl(new_ext_vertices, new_vertices, i, num_workers, rc_counter, mtx, cv);
        });
    }

    std::unique_lock<std::mutex>lock(mtx);
    cv.wait(lock, [&rc_counter, num_workers](){ return rc_counter.load() > num_workers; });
    lock.unlock();

    return new_vertices;
}

std::vector<Vertex> GCGraph::updateGraphEngine(std::vector<std::pair<ExtEdge, int>>& add_edges,
            std::vector<std::pair<ExtEdge, int>>& delete_edges, std::vector<Vertex>& new_vertices) {
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<int> rc_counter;
    const int num_workers = num_rc_workers_;

    std::shared_lock<std::shared_mutex> engine_lock1(engine_mtx_.mtx_);
    // update engine (add edges)
    rc_counter.store(0);
    for (int i = 0; i < num_workers; i++) {
        boost::asio::post(rc_workers_->workers_, [this, &add_edges, i, num_workers, 
                &rc_counter, &mtx, &cv](){ 
            applyAddEdges(add_edges, i, num_workers, rc_counter, mtx, cv);
        });
    }

    std::unique_lock<std::mutex>lock(mtx);
    cv.wait(lock, [&rc_counter, num_workers](){ return rc_counter.load() > num_workers; });
    lock.unlock();
    engine_lock1.unlock();

    std::shared_lock<std::shared_mutex> engine_lock2(engine_mtx_.mtx_);
    // update engine (delete edges), identify vertices with ref count of 0 & return as vectors
    std::vector<std::vector<Vertex>> delete_vertices;
    for (int i = 0; i < num_workers; i++) {
        boost::asio::post(rc_workers_->workers_, [this, new_vertices, &delete_edges, i, num_workers, 
                &delete_vertices, &mtx, &cv](){ 
            applyDeleteEdges(delete_edges, i, num_workers, delete_vertices, mtx, cv);
        });
    }

    lock.lock();
    cv.wait(lock, [&delete_vertices, num_workers](){ return delete_vertices.size() >= num_workers; });
    lock.unlock();
    engine_lock2.unlock();

    // WARN shared lock also makes sure that this is mutually exclusive with the reset color function of mark-sweep
    std::shared_lock<std::shared_mutex> engine_lock3(engine_mtx_.mtx_);
    // color the vertices so they can be included in the next round of ms
    activateVertices(new_vertices);
    engine_lock3.unlock();

    return mergeVectors(delete_vertices, true, false);
}

std::vector<ExtEdge> GCGraph::execRC(std::vector<Vertex>& delete_vertices, std::vector<ExtVertex>& reduce_buf) {
    std::mutex mtx;
    std::condition_variable cv;
    const int num_workers = num_rc_workers_;

    // execute local tracing for reference counting
    std::vector<std::vector<ExtVertex>> unreachable_vertices;
    std::vector<std::vector<ExtEdge>> unreachable_edges;
    std::shared_lock<std::shared_mutex> engine_lock(engine_mtx_.mtx_);
    for (int i = 0; i < num_workers; i++) {
        boost::asio::post(rc_workers_->workers_, [this, &delete_vertices, i, num_workers, 
                &unreachable_vertices, &unreachable_edges, &mtx, &cv](){ 
            execRCImpl(delete_vertices, i, num_workers, unreachable_vertices, unreachable_edges,
                    mtx, cv);
        });
    }

    std::unique_lock<std::mutex>lock(mtx);
    cv.wait(lock, [&unreachable_vertices, num_workers](){ return unreachable_vertices.size() >= num_workers; });
    lock.unlock();
    engine_lock.unlock();

    // merge all the results
    reduce_buf = std::move(mergeVectors(unreachable_vertices, true, false));
    return mergeVectors(unreachable_edges, false, false);
}

void GCGraph::postRCCommit(std::vector<ExtVertex>& delete_vertices, std::vector<std::pair<ExtEdge, int>>& delete_edges,
    std::vector<ExtEdge>& unreachable_edges) {
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<int> rc_counter;
    const int num_workers = num_rc_workers_;

    // clean up vertex maps & delete edges in the graph store
    rc_counter.store(0);
    for (int i = 0; i < num_workers; i++) {
        boost::asio::post(rc_workers_->workers_, [this, &delete_vertices, &delete_edges, &unreachable_edges, i, 
            num_workers, &rc_counter, &mtx, &cv](){ 
            postRCCommitImpl(delete_vertices, delete_edges, unreachable_edges, i, num_workers, rc_counter, mtx, cv);
        });
    }

    std::unique_lock<std::mutex>lock(mtx);
    cv.wait(lock, [&rc_counter, num_workers](){ return rc_counter.load() > num_workers; });
    lock.unlock();

}

void GCGraph::postRCCommitImpl(std::vector<ExtVertex>& delete_vertices, std::vector<std::pair<ExtEdge, int>>& delete_edges, 
        std::vector<ExtEdge>& unreachable_edges, int worker_id, int num_workers, std::atomic<int>& rc_counter,
        std::mutex& mtx, std::condition_variable& cv) {
    std::pair<size_t, size_t> vertices_range = partitionRange(delete_vertices.size(), worker_id, num_workers);
    rocksdb::WriteBatch batch;
    rocksdb::Status status;

    // delete entries from vertex map
    for (size_t i = vertices_range.first ; i < vertices_range.second; i++) {
        ExtVertex ext_vertex = delete_vertices[i];
        Vertex vertex;
        bool found = i_vertex_map_.visit(ext_vertex, [&](const auto& x){ vertex = x.second; });
        if (found) {
            i_vertex_map_.erase(ext_vertex);
            o_vertex_map_.erase_if(vertex, [&](const auto& x){ return (ext_vertex == x.second); });
        }
    }        

    // delete deleted edges that are unreachable
    for (size_t i = 0; i < delete_edges.size(); i++) {
        if (delete_edges[i].second < 0 && hash_value(delete_edges[i].first) % num_workers == worker_id) {
            ExtEdge key = delete_edges[i].first;
            // serialize
            key.serialize();

            status = batch.Delete(add_edges_[worker_id], rocksdb::Slice(reinterpret_cast<char*>(&key), sizeof(key)));
            status = batch.Delete(delete_edges_[worker_id], rocksdb::Slice(reinterpret_cast<char*>(&key), sizeof(key)));
        }
    }

    // delete unreachable edges
    for (size_t i = 0; i < unreachable_edges.size(); i++) {
        if (hash_value(unreachable_edges[i])%num_workers == worker_id) {
            ExtEdge key = unreachable_edges[i];
            // serialize
            key.serialize();

            status = batch.Delete(add_edges_[worker_id], rocksdb::Slice(reinterpret_cast<char*>(&key), sizeof(key)));
            status = batch.Delete(delete_edges_[worker_id], rocksdb::Slice(reinterpret_cast<char*>(&key), sizeof(key)));
        }
    }

    // write batch to rocksdb
    status = graph_db_[worker_id]->Write(write_options_, &batch);

    // vote & notify if the last thread
    int order = rc_counter.fetch_add(1); 
    if (order >= num_workers - 1) {
        std::unique_lock<std::mutex>lock(mtx);
        rc_counter.fetch_add(1);
        lock.unlock();
        cv.notify_one();
    }
}


void GCGraph::execRCImpl(std::vector<Vertex>& delete_vertices, int worker_id, int num_workers, 
        std::vector<std::vector<ExtVertex>>& reduce_buffer1, std::vector<std::vector<ExtEdge>>& reduce_buffer2,
        std::mutex& mtx, std::condition_variable& cv) {
    std::pair<size_t, size_t> vertices_range = partitionRange(delete_vertices.size(), worker_id, num_workers);
    std::vector<ExtVertex> unreachable_vertices;
    std::vector<ExtEdge> unreachable_edges;
    
    // initialize the active set
    // The entire edge lists are removed and the vertex desc is reset for efficiency.
    // The edge lists are deallocated only after the edges are processed
    std::stack<ExtVertex> src_vertices;
    std::stack<std::pair<bool,void*>> edge_lists;
    for (size_t i = vertices_range.first; i < vertices_range.second; i++) {
        Vertex vertex = delete_vertices[i];
        Page* page = graph_engine_[vertex / Page::capacity_];
        std::unique_lock<std::shared_mutex> page_lock(page->mtx_);
        bool free = page->filled();
        VertexDesc& vertex_desc = page->vertex_descs_[vertex % Page::capacity_];
        uint64_t state = vertex_desc.state_.load();
        // if vertex desc is allocated (mark-sweep could have already deallocated))
        if (state & V_ALLOC_TRUE) {
            ExtVertex ext_vertex;
            bool found = o_vertex_map_.visit(vertex, [ &ext_vertex ](const auto& x){ ext_vertex = x.second; });
            unreachable_vertices.push_back(ext_vertex);
            // check for any edge list and add to active set
            state &= V_EDGE_TYPE_MASK;
            if (state == V_EDGE_TYPE_VECTOR) {
                src_vertices.push(ext_vertex);
                edge_lists.push(std::pair<bool,void*>(true, vertex_desc.edge_list_));
            }
            else if (state == V_EDGE_TYPE_MAP) {
                src_vertices.push(ext_vertex);
                edge_lists.push(std::pair<bool,void*>(false, vertex_desc.edge_list_));
            }
            vertex_desc.state_.store(0);
            vertex_desc.edge_list_ = nullptr;
            page->size_--;
        }
        free ^= page->filled();
        page_lock.unlock();

        if (free) {
            free_pages_.push(page);
        }
    }

    while (!edge_lists.empty()) {
        ExtVertex src_ext_vertex = src_vertices.top();
        std::pair<bool,void*> edge_list = edge_lists.top();
        src_vertices.pop();
        edge_lists.pop();
        
        // iterate the vector
        if (edge_list.first) {
            std::vector<Vertex>* neighbors = reinterpret_cast<std::vector<Vertex>*>(edge_list.second);
            for (auto& vertex : *neighbors) {
                if (vertex != INVALID_VERTEX) {
                    // only the main rc thread will update the catalog & perform postRCCommit protocol
                    // after getting buffered result from the main ms thread, so the vertex map
                    // should be valid even if ms identified the unreachable vertex. 
                    ExtVertex ext_vertex;
                    bool found = o_vertex_map_.visit(vertex, [&ext_vertex](const auto& x){ ext_vertex = x.second; });
                    unreachable_edges.emplace_back(src_ext_vertex, ext_vertex);
                    Page* page = graph_engine_[vertex / Page::capacity_];
                    std::unique_lock<std::shared_mutex> page_lock(page->mtx_);
                    bool free = page->filled();
                    VertexDesc& vertex_desc = page->vertex_descs_[vertex % Page::capacity_];
                    uint64_t state = vertex_desc.state_.load();
                    // if vertex still exists (mark-sweep could have already deallocated)
                    if (state & V_ALLOC_TRUE) {
                        uint32_t ref_count = vertex_desc.fetchSubRef(1);
                        // if the last reference count
                        if (ref_count == 1) {
                            unreachable_vertices.push_back(ext_vertex);
                            // check for any edge list and add to active set
                            state &= V_EDGE_TYPE_MASK;
                            if (state == V_EDGE_TYPE_VECTOR) {
                                src_vertices.push(ext_vertex);
                                edge_lists.push(std::pair<bool,void*>(true, vertex_desc.edge_list_));
                            }
                            else if (state == V_EDGE_TYPE_MAP) {
                                src_vertices.push(ext_vertex);
                                edge_lists.push(std::pair<bool,void*>(false, vertex_desc.edge_list_));
                            }
                            vertex_desc.state_.store(0);
                            vertex_desc.edge_list_ = nullptr;
                            page->size_--;
                        }
                    }
                    free ^= page->filled();
                    page_lock.unlock();

                    if (free) {
                        free_pages_.push(page);
                    }
                }
            }
            delete neighbors;
        }
        // iterate the flat map
        else {
            boost::unordered_flat_map<Vertex, int>* neighbors = 
                    reinterpret_cast<boost::unordered_flat_map<Vertex, int>*>(edge_list.second);
            for (auto& vertex : *neighbors) {
                ExtVertex ext_vertex;
                bool found = o_vertex_map_.visit(vertex.first, [&](const auto& x){ ext_vertex = x.second; });
                unreachable_edges.emplace_back(src_ext_vertex, ext_vertex);
                bool free = false;
                Page* page = graph_engine_[vertex.first / Page::capacity_];
                for (int j = 0; j < vertex.second; j++) {
                    std::unique_lock<std::shared_mutex> page_lock(page->mtx_);
                    free = page->filled();
                    VertexDesc& vertex_desc = page->vertex_descs_[vertex.first % Page::capacity_];
                    uint64_t state = vertex_desc.state_.load();
                    // if vertex still exists (mark-sweep could have already deallocated)
                    if (state & V_ALLOC_TRUE) {
                        uint32_t ref_count = vertex_desc.fetchSubRef(1);
                        // if the last reference count
                        if (ref_count == 1) {
                            unreachable_vertices.push_back(ext_vertex);
                            // check for any edge list and add to active set
                            state &= V_EDGE_TYPE_MASK;
                            if (state == V_EDGE_TYPE_VECTOR) {
                                src_vertices.push(ext_vertex);
                                edge_lists.push(std::pair<bool,void*>(true, vertex_desc.edge_list_));
                            }
                            else if (state == V_EDGE_TYPE_MAP) {
                                src_vertices.push(ext_vertex);
                                edge_lists.push(std::pair<bool,void*>(false, vertex_desc.edge_list_));
                            }
                            vertex_desc.state_.store(0);
                            vertex_desc.edge_list_ = nullptr;
                            page->size_--;
                        }
                    }
                    free ^= page->filled();
                    page_lock.unlock();
                }

                if (free) {
                    free_pages_.push(page);
                }

            }
            delete neighbors;
        }

    }

    // push the local vectors to reduce buffers
    std::unique_lock<std::mutex> lock(mtx);
    reduce_buffer1.push_back(std::move(unreachable_vertices));
    reduce_buffer2.push_back(std::move(unreachable_edges));
    size_t order = reduce_buffer1.size();
    lock.unlock();
    if (order >= num_workers) {
        cv.notify_one();
    }
}

void GCGraph::applyDeleteEdges(std::vector<std::pair<ExtEdge,int>>& delete_edges, int worker_id, int num_workers,
        std::vector<std::vector<Vertex>>& reduce_buffer, std::mutex& mtx,
        std::condition_variable& cv) {
    std::vector<Vertex> delete_vertices;        
    std::pair<size_t, size_t> vertices_range = partitionRange(delete_edges.size(), worker_id, num_workers);
    
    for (size_t i = vertices_range.first ; i < vertices_range.second; i++) {
        Vertex src;
        bool found = i_vertex_map_.visit(delete_edges[i].first.src_, [&](const auto& x){ src = x.second; });
        
        Vertex dest;
        found = found && i_vertex_map_.visit(delete_edges[i].first.dest_, [&](const auto& x){ dest = x.second; });
        if (found) {
            // because we might change edge count in the loop
            int edge_count = delete_edges[i].second;
            for (int j = 0; j < edge_count; j++) {
                Page* src_page = graph_engine_[src / Page::capacity_];
                std::unique_lock<std::shared_mutex> src_page_lock(src_page->mtx_);
                VertexDesc& src_vertex_desc = src_page->vertex_descs_[src % Page::capacity_];
                found = src_vertex_desc.remove(dest);
                src_page_lock.unlock();
                // if found, decrement the ref count
                if (found) {
                    Page* dest_page = graph_engine_[dest / Page::capacity_];
                    std::unique_lock<std::shared_mutex> dest_page_lock(dest_page->mtx_);
                    VertexDesc& dest_vertex_desc = dest_page->vertex_descs_[dest % Page::capacity_];
                    // could have been deleted by mark-sweep after the edge is dropped
                    if (dest_vertex_desc.state_.load() & V_ALLOC_TRUE) {
                        uint32_t ref_count = dest_vertex_desc.fetchSubRef(1);
                        dest_page_lock.unlock();
                        if (ref_count == 1) {
                            delete_vertices.push_back(dest);
                            // WARNING a bit of hack so the delete edges that become unreachable are 
                            // properly handled in postrccommit
                            delete_edges[i].second = -1;
                        }
                    }
                }
            }
        }
    }

    // push unreachable vertices to the reduce buffer & notify if the last thread
    std::unique_lock<std::mutex> lock(mtx);
    reduce_buffer.push_back(std::move(delete_vertices));
    size_t order = reduce_buffer.size();
    lock.unlock();
    if (order >= num_workers) {
        cv.notify_one();
    }

}

void GCGraph::applyAddEdges(std::vector<std::pair<ExtEdge,int>>& add_edges, int worker_id, int num_workers,
        std::atomic<int>& rc_counter, std::mutex& mtx, std::condition_variable& cv) {
    std::pair<size_t, size_t> vertices_range = partitionRange(add_edges.size(), worker_id, num_workers);
    
    for (size_t i = vertices_range.first ; i < vertices_range.second; i++) {
        Vertex src;
        bool found = i_vertex_map_.visit(add_edges[i].first.src_, [&](const auto& x){ src = x.second; });
        
        Vertex dest;
        found = i_vertex_map_.visit(add_edges[i].first.dest_, [&](const auto& x){ dest = x.second; });
        int edge_count = add_edges[i].second;
        for (int j = 0; j < edge_count; j++) {
            Page* src_page = graph_engine_[src / Page::capacity_];
            std::unique_lock<std::shared_mutex> src_page_lock(src_page->mtx_);
            VertexDesc& src_vertex_desc = src_page->vertex_descs_[src % Page::capacity_];
            src_vertex_desc.insert(dest);
            src_page_lock.unlock();
            // increment the rc
            Page* dest_page = graph_engine_[dest / Page::capacity_];
            std::unique_lock<std::shared_mutex> dest_page_lock(dest_page->mtx_);
            VertexDesc& dest_vertex_desc = dest_page->vertex_descs_[dest % Page::capacity_];
            dest_vertex_desc.fetchAddRef(1);
            dest_page_lock.unlock();
        }
    }

    int order = rc_counter.fetch_add(1); 
    if (order >= num_workers - 1) {
        std::unique_lock<std::mutex>lock(mtx);
        rc_counter.fetch_add(1);
        lock.unlock();
        cv.notify_one();
    }

}

void GCGraph::updateVertexMapsImpl(std::vector<ExtVertex>& new_ext_vertices, std::vector<Vertex>& new_vertices,
        int worker_id, int num_workers, std::atomic<int>& rc_counter, std::mutex& mtx, 
        std::condition_variable& cv) {
    std::pair<size_t, size_t> vertices_range = partitionRange(new_ext_vertices.size(), worker_id, num_workers);
    
    // check if either of the vertices already exist & add edges
    for (size_t i = vertices_range.first ; i < vertices_range.second; i++) {
        i_vertex_map_.insert_or_assign(new_ext_vertices[i], new_vertices[i]);
        o_vertex_map_.insert_or_assign(new_vertices[i], new_ext_vertices[i]);
    }

    int order = rc_counter.fetch_add(1); 
    if (order >= num_workers - 1) {
        std::unique_lock<std::mutex>lock(mtx);
        rc_counter.fetch_add(1);
        lock.unlock();
        cv.notify_one();
    }
}

void GCGraph::activateVertices(std::vector<Vertex>& vertices) {
    for (const auto& vertex : vertices) {
        Page* page = graph_engine_[vertex / Page::capacity_];
        std::unique_lock<std::shared_mutex> page_lock(page->mtx_);
        VertexDesc& vertex_desc = page->vertex_descs_[vertex % Page::capacity_];
        uint64_t state = vertex_desc.state_.load();
        vertex_desc.state_.store(state | V_COLOR_GREEN);
        page_lock.unlock();
    }
}

std::vector<Vertex> GCGraph::allocateVertices(int num_vertex) {
    std::vector<Vertex> result;
    result.reserve(num_vertex);
    Page* cur_page;
    bool filled = true;
    while (result.size() < num_vertex && free_pages_.pop(cur_page)) {
        std::unique_lock<std::shared_mutex> page_lock(cur_page->mtx_);
        VertexDesc* vertex_descs = cur_page->vertex_descs_;
        // traverse all vertices on the page
        for (uint16_t i = 0; i < Page::capacity_ && result.size() < num_vertex; i++) {
            uint64_t state = vertex_descs[i].state_.load();
            Vertex vid = cur_page->page_id_ * Page::capacity_ +  i;
            // ms might have deleted the vertex from the engine, 
            // WARN but underfilled pages (due to ms) are added to free list only after the main rc 
            // thread has finished updating the vertex map
            if (!(state & V_ALLOC_TRUE)) {
                vertex_descs[i].state_.store(V_ALLOC_TRUE);
                result.push_back(vid);
                cur_page->size_++;
            }
        }
        filled = cur_page->filled();
        page_lock.unlock();
    }

    // if the page still below the threshold, push it back to the queue
    if (!filled) {
        free_pages_.push(cur_page);
        filled = true;
    }

    if (result.size() < num_vertex) {
        // We assume there is only one thread allocating vid at a time, so this is allowed.
        // TODO implement an optimistic mechanism if we end up introducing concurrent allocations
        std::shared_lock<std::shared_mutex> shared_engine_lock(engine_mtx_.mtx_);
        PageId new_page_id = graph_engine_.size();
        shared_engine_lock.unlock();
        
        Page* new_page;
        std::vector<Page*> new_pages;
        new_pages.reserve((num_vertex - result.size()) / Page::capacity_ + 1);
        while (result.size() < num_vertex) {
            new_page = new Page();
            new_page->page_id_ = new_page_id;
            VertexDesc* vertex_descs = new_page->vertex_descs_;
            for (uint16_t i = 0; i < Page::capacity_ && result.size() < num_vertex; i++) {
                result.push_back(new_page_id * Page::capacity_ +  i);
                vertex_descs[i].state_.store(V_ALLOC_TRUE);
                new_page->size_++;
            }
            filled = new_page->filled();
            new_pages.push_back(new_page);
            new_page_id++;
        }

        std::unique_lock<std::shared_mutex> unique_engine_lock(engine_mtx_.mtx_);
        graph_engine_.insert(graph_engine_.end(), new_pages.begin(), new_pages.end());
        unique_engine_lock.unlock();

        // if page is not filled, push it to the queue
        if (!filled) {
            free_pages_.push(new_page);
        }
    }

    return result;
}




void GCGraph::updateGraphStoreImpl(std::vector<std::pair<ExtEdge, int>>& add_edges,
        std::vector<std::pair<ExtEdge, int>>& delete_edges,
        EpochId epoch_id, const std::string& delete_time, int worker_id, 
        int num_workers, std::vector<std::vector<ExtVertex>>& reduce_buffer, std::mutex& mtx, 
        std::condition_variable& cv) {
    std::vector<ExtVertex> new_ext_vertices;
    rocksdb::WriteBatch batch;
    
    epoch_id = htole64(epoch_id);
    ExtEdge key;
    std::string value;
    rocksdb::Status status;
    // check if either of the vertices already exist & add edges
    for (size_t i = 0; i < add_edges.size(); i++) {
        if (hash_value(add_edges[i].first)%num_workers == worker_id) {
            ExtVertex src = add_edges[i].first.src_;
            ExtVertex dest = add_edges[i].first.dest_;
            //check src
            if (!i_vertex_map_.contains(src)) {
                new_ext_vertices.push_back(src);
            }
            // check dest
            if (!i_vertex_map_.contains(dest)) {
                new_ext_vertices.push_back(dest);
            }
            // serialize
            key.src_ = src;
            key.dest_ = dest;
            key.serialize();
            // get the previous log count value if it exists
            rocksdb::Status s = graph_db_[worker_id]->Get(read_options_, add_edges_[worker_id],
                    rocksdb::Slice(reinterpret_cast<char*>(&key), sizeof(key)), &value);
            int32_t log_count = 0;
            if (s.ok()) {
                const char* count_ptr = value.data() + value.size() - sizeof(int32_t);
                std::memcpy(&log_count, count_ptr, sizeof(int32_t));
            }

            log_count = le32toh(log_count);
            // update the log count value
            log_count += add_edges[i].second;
            log_count = htole32(log_count);
            value.clear();
            // new epoch id and log count
            value.append(reinterpret_cast<char*>(&epoch_id), sizeof(epoch_id));
            value.append(reinterpret_cast<char*>(&log_count), sizeof(log_count));
            batch.Put(add_edges_[worker_id], rocksdb::Slice(reinterpret_cast<char*>(&key), sizeof(key)),
                    rocksdb::Slice(value));
            value.clear();
        }
    }

    // TODO check if the edge exists in addedges before applying to the write batch
    // update delete edges
    for (size_t i = 0; i < delete_edges.size(); i++) {
        if (hash_value(delete_edges[i].first)%num_workers == worker_id) {
            ExtVertex src = delete_edges[i].first.src_;
            ExtVertex dest = delete_edges[i].first.dest_;
            // serialize
            key.src_ = src;
            key.dest_ = dest;
            key.serialize();
            // get the previous log count value if it exists
            rocksdb::Status s = graph_db_[worker_id]->Get(read_options_, delete_edges_[worker_id],
                    rocksdb::Slice(reinterpret_cast<char*>(&key), sizeof(key)), &value);
            int32_t log_count = 0;
            if (s.ok()) {
                const char* count_ptr = value.data() + value.size() - sizeof(int32_t);
                std::memcpy(&log_count, count_ptr, sizeof(int32_t));
            }
            
            log_count = le32toh(log_count);
            // update the log count value
            log_count += delete_edges[i].second;
            log_count = htole32(log_count);
            value.clear();
            // delete time, epoch_id, and log count
            value.append(delete_time);
            value.append(reinterpret_cast<char*>(&epoch_id), sizeof(epoch_id));
            value.append(reinterpret_cast<char*>(&log_count), sizeof(log_count));
            batch.Put(delete_edges_[worker_id], rocksdb::Slice(reinterpret_cast<char*>(&key), sizeof(key)),
                    rocksdb::Slice(value));
            value.clear();
        }
    }

    // write batch to rocksdb
    graph_db_[worker_id]->Write(write_options_, &batch);

    // fsync to make the changes durable
    graph_db_[worker_id]->SyncWAL();

    // push new vertices to the reduce buffer & notify if the last thread
    std::unique_lock<std::mutex> lock(mtx);
    reduce_buffer.push_back(std::move(new_ext_vertices));
    size_t order = reduce_buffer.size();
    lock.unlock();
    if (order >= num_workers) {
        cv.notify_one();
    }
    
}

std::pair<size_t, size_t> GCGraph::partitionRange(size_t range, int idx, int num_partitions) {
    std::pair<size_t, size_t> result(0, 0);
    if (idx >= range) {
        return result;
    }
    
    if (num_partitions >= range) {
        result.first = idx;
        result.second = idx + 1;
        return result;
    }

    size_t base_size = range / num_partitions;
    size_t remainder = range % num_partitions;
    size_t partition_size = base_size + (idx < remainder ? 1 : 0);
    result.first = (idx * base_size) + std::min(static_cast<size_t>(idx), remainder);
    result.second = result.first + partition_size;

    return result;
}

template<typename T>
std::vector<T> GCGraph::mergeVectors(std::vector<std::vector<T>>& vectors, bool sort, bool dedup) {
    size_t total_size = 0;
    for (auto& inner_vec : vectors) {
        total_size += inner_vec.size();
    }

    std::vector<T> result;
    result.reserve(total_size);

    for (auto& inner_vec : vectors) {
        result.insert(result.end(), inner_vec.begin(), inner_vec.end());
    }

    if (sort || dedup) {
        std::sort(result.begin(), result.end());
        if (dedup) {
            auto it = std::unique(result.begin(), result.end());
            result.erase(it, result.end());
        }
    }
    
    return result;
}

void GCGraph::shutDown() {
    if (!shut_down_) {
        // shutdown the thread pool
        rc_workers_->shutDown();
        ms_workers_->shutDown();

        // shutdown graph engine
        // deallocate all the pages
        std::unique_lock<std::shared_mutex> lock(engine_mtx_.mtx_);
        for (auto& page : graph_engine_) {
            delete page;
            page = nullptr;
        }
        lock.unlock();
        // empty out the free pages queue 
        Page* page;
        while (!free_pages_.empty()) {
            free_pages_.pop(page);
        }

        for (size_t i = 0; i < graph_db_.size(); i++) {
            // shutdown graph store (rocksdb)
            graph_db_[i]->DestroyColumnFamilyHandle(add_edges_[i]);
            graph_db_[i]->DestroyColumnFamilyHandle(delete_edges_[i]);
            delete graph_db_[i];
        }
    }
    shut_down_ = true;
}

GCGraph& GCGraph::getInstanceImpl() {
    static GCGraph instance;
    return instance;
}

GCGraph::GCGraph() : shut_down_(false) {
    // get configs
    const Config& config = Config::getInstance();
    std::string db_path = config.get("gc.graph.path");
    std::string create = config.get("gc.graph.create");
    int max_pages = std::stoi(config.get("gc.graph.max_pages"));
    // num_rc_workers is also the number of shards for now
    num_rc_workers_ = std::stoi(config.get("gc.graph.num_rc_workers"));
    num_ms_workers_ = std::stoi(config.get("gc.graph.num_ms_workers"));

    //set table options
    rocksdb::BlockBasedTableOptions table_options;
    table_options.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10, false));
    table_options.cache_index_and_filter_blocks = true;
    table_options.pin_l0_filter_and_index_blocks_in_cache = true;
    // table_options.index_type = rocksdb::BlockBasedTableOptions::kBinarySearchWithFirstKey;
    // table_options.block_size = 16 * 1024;
    std::shared_ptr<rocksdb::TableFactory> table_factory(rocksdb::NewBlockBasedTableFactory(table_options));
    col_family_options_.table_factory = table_factory;
    // set up compression options
    // col_family_options_.compression = rocksdb::kNoCompression;
   
    graph_db_.resize(num_rc_workers_);
    add_edges_.resize(num_rc_workers_);
    delete_edges_.resize(num_rc_workers_);
    for (int i = 0; i < num_rc_workers_; i++) {
         // set db options
        rocksdb::Options db_options;
        db_options.IncreaseParallelism(3);
        db_options.OptimizeLevelStyleCompaction();
        db_options.create_if_missing = true;
        db_options.max_background_jobs = 2;

        if (create == "true") {
            std::unique_ptr<rocksdb::DB> temp_db;
            rocksdb::Status status = rocksdb::DB::Open(db_options, db_path + std::to_string(i), &temp_db);
            if (status.ok()) {
                graph_db_[i] = temp_db.release(); // Transfers ownership to your raw pointer
            }
            assert(status.ok());
            status = graph_db_[i]->CreateColumnFamily(col_family_options_, "add_edges", &add_edges_[i]);
            assert(status.ok());
            status = graph_db_[i]->CreateColumnFamily(col_family_options_, "delete_edges", &delete_edges_[i]);
            assert(status.ok());
        }
        else {
            std::vector<rocksdb::ColumnFamilyDescriptor> column_families;
            std::vector<rocksdb::ColumnFamilyHandle*> handles;
            column_families.reserve(3);
            column_families.push_back(rocksdb::ColumnFamilyDescriptor());
            column_families.push_back(rocksdb::ColumnFamilyDescriptor("add_edges", col_family_options_));
            column_families.push_back(rocksdb::ColumnFamilyDescriptor("delete_edges", col_family_options_));
            
            std::unique_ptr<rocksdb::DB> temp_db_cf;

            // 2. Pass it into the Open method (replacing &graph_db_[i])
            rocksdb::Status status = rocksdb::DB::Open(db_options, db_path + std::to_string(i), column_families,
                    &handles, &temp_db_cf);

            // 3. If successful, transfer ownership to your raw pointer array
            if (status.ok()) {
                graph_db_[i] = temp_db_cf.release();
            }

            assert(status.ok());
            // destroy default column family handle
            graph_db_[i]->DestroyColumnFamilyHandle(handles[0]);
            add_edges_[i] = handles[1];
            delete_edges_[i] = handles[2];
            handles.clear();     
        }
    }

    read_options_.async_io = true;
    read_options_.readahead_size = 0;
    read_options_.adaptive_readahead = true;

    // reserve the pages vector, so the array is not reallocated when a new page is added
    graph_engine_.reserve(max_pages);

    // intialize thread pool
    rc_workers_ = std::make_unique<ThreadPool>(5 * num_rc_workers_);
    ms_workers_ = std::make_unique<ThreadPool>(num_ms_workers_);

    // allocate the root
    std::vector<Vertex> root_vid = allocateVertices(1);
    i_vertex_map_.emplace(ExtVertex(0,0,0), root_vid[0]);
    o_vertex_map_.emplace(root_vid[0], ExtVertex(0,0,0));

}
    
}
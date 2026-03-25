#pragma once

#include <cstdint>
#include <shared_mutex>
#include <utility>

#include "boost/asio/thread_pool.hpp"
#include "boost/unordered/unordered_flat_map.hpp"
#include "boost/unordered/concurrent_flat_map.hpp"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "utils/concurrentqueue.h"
#include "utils/types.h"
#include "utils/paddedmutex.h"
#include "utils/threadpool.h"

namespace ercat {

const unsigned int PAGE_SIZE = 16384;
// number of references to the vertex
constexpr uint32_t V_REFCOUNT_BITS = 32;
// size of the edge list (sticky)
constexpr uint32_t V_EDGE_SIZE_BITS = 6;
// edge list type: null, vector or hash map
constexpr uint32_t V_EDGE_TYPE_BITS = 2;
// vertex color: purple, white, or black
constexpr uint32_t V_COLOR_BITS = 2;
// whether the vertex is allocated
constexpr uint32_t V_ALLOC_BITS = 1;

constexpr uint64_t V_REFCOUNT_ONE = 1UL;
constexpr uint64_t V_REFCOUNT_MASK = ((1UL << V_REFCOUNT_BITS) - 1);

// approx edge size that does not take delete into account
// also does not keep track once vector is turned to hash map
constexpr uint64_t V_EDGE_SIZE_MASK = (((1UL << V_EDGE_SIZE_BITS) - 1) << (32));
constexpr uint64_t V_EDGE_SIZE_ONE = (1UL << 32);

constexpr uint64_t V_EDGE_TYPE_MASK = (((1UL << V_EDGE_TYPE_BITS) - 1) << (38));
constexpr uint64_t V_EDGE_TYPE_NULL = 0UL;
constexpr uint64_t V_EDGE_TYPE_VECTOR = (1UL << 38);
constexpr uint64_t V_EDGE_TYPE_MAP = (2UL << 38);

constexpr uint64_t V_COLOR_MASK = (((1UL << V_COLOR_BITS) - 1) << (40));
constexpr uint64_t V_COLOR_PURPLE = 0UL;
constexpr uint64_t V_COLOR_GREEN = (1UL << 40);
constexpr uint64_t V_COLOR_WHITE = (2UL << 40);
constexpr uint64_t V_COLOR_BLACK = (3UL << 40);

constexpr uint64_t V_ALLOC_TRUE = (1UL << 42);

constexpr uint32_t INVALID_VERTEX = std::numeric_limits<uint32_t>::max();

typedef uint32_t PageId;
typedef uint32_t Vertex;

struct Edge {
    Edge() { }
    Edge(Vertex src, Vertex dest) : src_(src), dest_(dest) { }

    bool operator==(const Edge& other) const {
        return src_ == other.src_ && dest_ == other.dest_;
    }

    bool operator<(const Edge& other) const {
        return (src_ < other.src_) || (src_ == other.src_ && dest_ < other.dest_);
    }

    bool operator!=(const Edge& other) const {
        return !(*this == other); 
    }

    bool operator>(const Edge& other) const {
        return other < *this; 
    }

    bool operator<=(const Edge& other) const {
        return !(*this > other); 
    }

    bool operator>=(const Edge& other) const {
        return !(*this < other);
    }

    Vertex src_;
    Vertex dest_;
};

inline std::size_t hash_value(Edge const& edge) {
    std::size_t seed = 0;
    boost::hash_combine(seed, edge.src_);
    boost::hash_combine(seed, edge.dest_);
    return seed;
}

struct ExtVertex {
    ExtVertex() { }
    ExtVertex(EntId ent_id, VersionId vid, ObjId obj_id) : ent_id_(ent_id), vid_(vid), obj_id_(obj_id) { }

    bool operator==(const ExtVertex& other) const {
        return ent_id_ == other.ent_id_ && vid_ == other.vid_ && obj_id_ == other.obj_id_;
    }

    bool operator<(const ExtVertex& other) const {
        return (ent_id_ < other.ent_id_) ||
                (ent_id_ == other.ent_id_ && obj_id_ < other.obj_id_) ||
                (ent_id_ == other.ent_id_ && obj_id_ == other.obj_id_ && vid_ < other.vid_);
    }

    bool operator!=(const ExtVertex& other) const {
        return !(*this == other); 
    }

    bool operator>(const ExtVertex& other) const {
        return other < *this; 
    }

    bool operator<=(const ExtVertex& other) const {
        return !(*this > other); 
    }

    bool operator>=(const ExtVertex& other) const {
        return !(*this < other);
    }

    EntId ent_id_;
    VersionId vid_;
    ObjId obj_id_;
};

inline std::size_t hash_value(ExtVertex const& ext_vertex) {
    std::size_t seed = 0;
    boost::hash_combine(seed, ext_vertex.ent_id_);
    boost::hash_combine(seed, ext_vertex.vid_);
    boost::hash_combine(seed, ext_vertex.obj_id_);
    return seed;
}

struct ExtEdge {
    ExtEdge() { }
    ExtEdge(ExtVertex src, ExtVertex dest) : src_(src), dest_(dest) { }
    void serialize() {
        src_.ent_id_ = htole32(src_.ent_id_);
        src_.vid_ = htole32(src_.vid_);
        src_.obj_id_ = htole64(src_.obj_id_);
        dest_.ent_id_ = htole32(dest_.ent_id_);
        dest_.vid_ = htole32(dest_.vid_);
        dest_.obj_id_ = htole64(dest_.obj_id_);
    }

    bool operator==(const ExtEdge& other) const {
        return src_ == other.src_ && dest_ == other.dest_;
    }

    bool operator<(const ExtEdge& other) const {
        return (src_ < other.src_) || (src_ == other.src_ && dest_ < other.dest_);
    }

    bool operator!=(const ExtEdge& other) const {
        return !(*this == other); 
    }

    bool operator>(const ExtEdge& other) const {
        return other < *this; 
    }

    bool operator<=(const ExtEdge& other) const {
        return !(*this > other); 
    }

    bool operator>=(const ExtEdge& other) const {
        return !(*this < other);
    }

    ExtVertex src_;
    ExtVertex dest_;
};

inline std::size_t hash_value(ExtEdge const& ext_edge) {
    std::size_t seed = 0;
    boost::hash_combine(seed, ext_edge.src_.ent_id_);
    boost::hash_combine(seed, ext_edge.src_.vid_);
    boost::hash_combine(seed, ext_edge.src_.obj_id_);
    boost::hash_combine(seed, ext_edge.dest_.ent_id_);
    boost::hash_combine(seed, ext_edge.dest_.vid_);
    boost::hash_combine(seed, ext_edge.dest_.obj_id_);
    return seed;
}

struct VertexDesc {
    VertexDesc() {
        state_.store(0);
        edge_list_ = nullptr;
    }

    ~VertexDesc() {
        reset();
    }

    void reset() {
        uint64_t state = state_.load();
        // if vertex desc is allocated (vertex exists)
        if (state & V_ALLOC_TRUE) {
            // check for any edge list and deallocate if needed
            state &= V_EDGE_TYPE_MASK;
            if (state == V_EDGE_TYPE_VECTOR) {
                delete(reinterpret_cast<std::vector<Vertex>*>(edge_list_));
            }
            else if (state == V_EDGE_TYPE_MAP) {
                delete(reinterpret_cast<boost::unordered_flat_map<Vertex, int>*>(edge_list_));
            }
        }
        state_.store(0);
        edge_list_ = nullptr;
    }

    void insert(Vertex dest) {
        uint64_t state = state_.load();
        uint64_t edge_type = state & V_EDGE_TYPE_MASK;
        uint64_t edge_size = (state & V_EDGE_SIZE_MASK) >> 32;
        switch(edge_type) {
            case V_EDGE_TYPE_NULL: {
                std::vector<Vertex>* edge_list = new std::vector<Vertex>();
                edge_list->push_back(dest);
                edge_list_ = edge_list;
                edge_type = V_EDGE_TYPE_VECTOR;
                edge_size++;
            }
                break;
            case V_EDGE_TYPE_VECTOR: {
                std::vector<Vertex>* edge_list = reinterpret_cast<std::vector<Vertex>*>(edge_list_);
                if (edge_size < 32) {
                    edge_list->push_back(dest);
                    edge_size++;
                }
                else {
                    boost::unordered_flat_map<Vertex,int>* new_edge_list = new boost::unordered_flat_map<Vertex,int>();
                    new_edge_list->reserve(edge_list->size() + 1);
                    for (auto & edge : *edge_list) {
                        if (edge != INVALID_VERTEX) {
                            if (!new_edge_list->contains(edge)) {
                                new_edge_list->emplace(edge, 1);
                            }
                            else {
                                new_edge_list->insert_or_assign(edge, new_edge_list->at(edge) + 1);
                            }
                        }
                    }
                    // increment count if exists
                    if (new_edge_list->contains(dest)) {
                        new_edge_list->insert_or_assign(dest, new_edge_list->at(dest) + 1);
                    }
                    // else insert new entry with base count of 1
                    else {
                        new_edge_list->emplace(dest, 1);
                    }
                    edge_list_ = new_edge_list;
                    delete edge_list;
                    edge_type = V_EDGE_TYPE_MAP;
                }
            }
                break;
            case V_EDGE_TYPE_MAP: {
                boost::unordered_flat_map<Vertex, int>* edge_list = 
                        reinterpret_cast<boost::unordered_flat_map<Vertex, int>*>(edge_list_);
                // increment count if exists
                if (edge_list->contains(dest)) {
                    edge_list->insert_or_assign(dest, edge_list->at(dest) + 1);
                }
                // else insert new entry with base count of 1
                else {
                    edge_list->emplace(dest, 1);
                }
            }
                break;
            default:
                break;
        }
        state &= ~V_EDGE_TYPE_MASK;
        state &= ~V_EDGE_SIZE_MASK;
        state |= edge_type;
        state |= (edge_size << 32);
        state_.store(state);
    }

    bool remove(Vertex dest) {
        bool found = false;
        uint64_t state = state_.load();
        uint64_t edge_type = state & V_EDGE_TYPE_MASK;
        switch(edge_type) {
            case V_EDGE_TYPE_VECTOR: {
                std::vector<Vertex>* edge_list = reinterpret_cast<std::vector<Vertex>*>(edge_list_);
                auto it = std::find(edge_list->begin(), edge_list->end(), dest);
                found = (it != edge_list->end());
                if (found) {
                    *it = INVALID_VERTEX;
                }
            }
                break;
            case V_EDGE_TYPE_MAP: {
                boost::unordered_flat_map<Vertex, int>* edge_list = 
                        reinterpret_cast<boost::unordered_flat_map<Vertex,int>*>(edge_list_);
                found = edge_list->contains(dest);
                if (found) {
                    int edge_count = edge_list->at(dest);
                    if (edge_count > 1) {
                        edge_list->insert_or_assign(dest, edge_count - 1);
                    }
                    else {
                        edge_list->erase(dest);
                    }
                }
            }
                break;
            default:
                break;
        }
        
        return found;
    }

    uint32_t fetchAddRef(uint32_t a) {
        return (state_.fetch_add(a) & V_REFCOUNT_MASK);
    }

    uint32_t fetchSubRef(uint32_t a) {
        return (state_.fetch_sub(a) & V_REFCOUNT_MASK);
    }
    

    std::atomic<uint64_t> state_;
    void* edge_list_;
};

// GCGraph is more than just the graph structure, it contains all high level 
// procedures that process the internal graph structure during GC. 
// This way, the procedures are all contained in a single file without 
// exposing low level details to the outside. This will however make the file bloated...
class GCGraph {
public:
    static void init();
    static GCGraph& getInstance();
    template <typename T>
    static std::vector<T> mergeVectors(std::vector<std::vector<T>>& vectors, bool sort, bool dedup);

    ~GCGraph();
    // update the gc graph store fsync is called before the function is returned
    std::vector<std::vector<ExtVertex>> updateGraphStore(std::vector<std::pair<ExtEdge, int>>& add_edges,
            std::vector<std::pair<ExtEdge, int>>& delete_edges, EpochId epoch_id, const std::string& delete_time);
    // update the vertex maps
    std::vector<Vertex> updateVertexMaps(std::vector<ExtVertex>& new_ext_vertices);
    // Update the gc graph store and engine. Returns a vector of vertices with zero reference counts
    std::vector<Vertex> updateGraphEngine(std::vector<std::pair<ExtEdge, int>>& add_edges,
            std::vector<std::pair<ExtEdge, int>>& delete_edges, std::vector<Vertex>& new_vertices);
    // Execute reference counting to identify all edges and vertices that need to be deleted
    // along the way, all unreachable vertices and edges are deleted in the engine
    std::vector<ExtEdge> execRC(std::vector<Vertex>& delete_vertices, std::vector<ExtVertex>& reduce_buf);
    // post procedure after the catalog is updated to update the graph store
    // fsync is not called because whatever changes needed to recover & resume gc is 
    // already persisted by fsync in the beginning. Worst case, we try deleting 
    // entities & relationships that have already been deleted in the catalog after recovery.
    void postRCCommit(std::vector<ExtVertex>& delete_vertices, std::vector<std::pair<ExtEdge, int>>& delete_edges,
            std::vector<ExtEdge>& unreachable_edges);
    // shut down the gc graph service 
    // 1. shut down the thread pool
    // 2. shut down rocksdb instance freeing all the resources
    // 3. deallocate all the pages in the memory engine
    void shutDown();

private:
    struct alignas(64) Page {
        static constexpr unsigned int capacity_ = (PAGE_SIZE - 64) / sizeof(VertexDesc);

        Page() : size_(0) { }
        ~Page() { }
        bool filled() {
            return ( static_cast<float>(size_)/capacity_ > 0.66);
        }

        // mutex for accessing the page
        std::shared_mutex mtx_;
        // PageId
        PageId page_id_;
        // number of actve vertex descs in the page
        uint16_t size_;
        // padding for cache alignment (may not be necessary)
        char padding_[64 - sizeof(mtx_) - sizeof(page_id_) - sizeof(size_)];
        // desc array
        VertexDesc vertex_descs_[capacity_];
    };

    static GCGraph& getInstanceImpl();
    static std::pair<size_t, size_t> partitionRange(size_t range, int idx, int num_partitions);

    GCGraph();
    void updateGraphStoreImpl(std::vector<std::pair<ExtEdge, int>>& add_edges, std::vector<std::pair<ExtEdge, int>>& delete_edges,
            EpochId epoch_id, const std::string& delete_time, int worker_id, 
            int num_workers, std::vector<std::vector<ExtVertex>>& reduce_buffer, std::mutex& mtx, 
            std::condition_variable& cv);
    void updateVertexMapsImpl(std::vector<ExtVertex>& new_ext_vertices, std::vector<Vertex>& new_vertices,
            int worker_id, int num_workers, std::atomic<int>& rc_counter, std::mutex& mtx,
            std::condition_variable& cv);
    void applyAddEdges(std::vector<std::pair<ExtEdge, int>>& add_edges, int worker_id, int num_workers,
            std::atomic<int>& rc_counter, std::mutex& mtx, std::condition_variable& cv);
    void applyDeleteEdges(std::vector<std::pair<ExtEdge, int>>& delete_edges, int worker_id, int num_workers,
            std::vector<std::vector<Vertex>>& reduce_buffer, std::mutex& mtx, 
            std::condition_variable& cv);
    std::vector<Vertex> allocateVertices(int num_vertex);
    void activateVertices(std::vector<Vertex>& vertices);
    void execRCImpl(std::vector<Vertex>& delete_vertices, int worker_id, int num_workers, 
            std::vector<std::vector<ExtVertex>>& reduce_buffer1, std::vector<std::vector<ExtEdge>>& reduce_buffer2,
            std::mutex& mtx, std::condition_variable& cv);
    void postRCCommitImpl(std::vector<ExtVertex>& delete_vertices, std::vector<std::pair<ExtEdge, int>>& delete_edges, 
        std::vector<ExtEdge>& unreachable_edges, int worker_id, int num_workers, std::atomic<int>& rc_counter,
        std::mutex& mtx, std::condition_variable& cv);

    // whether the graph engine is shut down
    bool shut_down_;

    // On-disk graph store for durability
    std::vector<rocksdb::DB*> graph_db_;
    // Edges that have been added
    std::vector<rocksdb::ColumnFamilyHandle*> add_edges_;
    // Edges that have been deleted
    std::vector<rocksdb::ColumnFamilyHandle*> delete_edges_;
    // config for all the column families
    rocksdb::ColumnFamilyOptions col_family_options_;
    // default read option
    rocksdb::ReadOptions read_options_;
    // default write option
    rocksdb::WriteOptions write_options_;
    
    // vertex maps for translation
    boost::concurrent_flat_map<ExtVertex, Vertex> i_vertex_map_;
    boost::concurrent_flat_map<Vertex, ExtVertex> o_vertex_map_;
    // global mutex for the graph engine
    PaddedSharedMutex engine_mtx_;
    // In-memory graph engine for analytics
    std::vector<Page*> graph_engine_;
    // pages whose size/capacity < threshold
    ConcurrentQueue<Page*> free_pages_;
    // worker threads for running graph analytics
    std::unique_ptr<ThreadPool> rc_workers_;
    std::unique_ptr<ThreadPool> ms_workers_;
    int num_rc_workers_;
    int num_ms_workers_;
};

}
#include <chrono>
#include "fmt/format.h"
#include <iterator>

#include "boost/asio/post.hpp"
#include "boost/functional/hash_fwd.hpp"
#include "boost/unordered/unordered_flat_map.hpp"

#include "gc/gcgraph.h"
#include "gc/gcmanager.h"
#include "utils/catcache.h"
#include "utils/config.h"

namespace ercat {

void GCManager::init() {
    getInstanceImpl();
}

GCManager& GCManager::getInstance() {
    return getInstanceImpl();
}

GCManager::~GCManager() {
    shutDown();
}

void GCManager::start() {
    std::atomic<bool>& stop = gc_ingestor_->stop_;
    boost::asio::post(gc_ingestor_->workers_, [this, &stop](){ ingestGC(stop); });
}

void GCManager::execGCTask(GCTask* gc_task) {
    boost::asio::post(gc_pipeline_[0]->workers_, [this, gc_task](){ handleGCTask(gc_task); });
}

void GCManager::shutDown() {
    if (!shut_down_) {
        gc_ingestor_->shutDown();
        for (auto& thread_pool : gc_pipeline_) {
            thread_pool->shutDown();
        }
        gc_graph_.shutDown();
        file_manager_.shutDown();
        shut_down_ = true;
    }
}

GCManager& GCManager::getInstanceImpl() {
    static GCManager instance;
    return instance;
}

// TODO
GCManager::GCManager() : gc_graph_(GCGraph::getInstance()), file_manager_(FileManager::getInstance()),
        shut_down_(false) {
    const Config& config = Config::getInstance();
    
    gc_ingestor_ = std::make_unique<ThreadPool>(1);
    for (int i = 0; i <= GCTask::max_task_state_; i++) {
        gc_pipeline_.push_back(std::make_unique<ThreadPool>(1));
    }
}

void GCManager::ingestGC(std::atomic<bool>& stop) {
    const std::chrono::milliseconds epoch_period(std::stoi(Config::getInstance().get("gc.epoch_period")));
    const std::string& pg_conn_str = Config::getPGConnString();
    PGconn* pg_conn = PGUtil::connectPG(pg_conn_str);
  
    std::string inc_epoch_str("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;"
            "UPDATE ercat.EpochId SET epoch_id = epoch_id + 1;"
            "UPDATE ercat.CurTime SET cur_time = NOW();"
            "COMMIT;");  

    std::string ingest_str("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;"
            "SELECT a.src_ent_id, a.src_vid, a.src_obj_id, a.dest_ent_id, a.dest_vid,"
            "a.dest_obj_id, COUNT(*) AS log_count "
            "FROM ercat.AddEdges a, ercat.EpochId e WHERE a.epoch_id < e.epoch_id - 1 "
            "GROUP BY a.src_ent_id, a.src_vid, a.src_obj_id, a.dest_ent_id, a.dest_vid, a.dest_obj_id;"
            "SELECT d.src_ent_id, d.src_vid, d.src_obj_id, d.dest_ent_id, d.dest_vid,"
            "d.dest_obj_id, COUNT(*) AS log_count "
            "FROM ercat.DeleteEdges d, ercat.EpochId e, ercat.CurTime c WHERE d.epoch_id < e.epoch_id - 1 "
            "AND d.delete_time < c.cur_time "
            "GROUP BY d.src_ent_id, d.src_vid, d.src_obj_id, d.dest_ent_id, d.dest_vid, d.dest_obj_id;"
            "SELECT e.epoch_id, c.cur_time FROM ercat.EpochId e, ercat.CurTime c;"
            "COMMIT;");

    while(!stop.load()) {
        // try increasing epoch in a tight loop
        PGUtil::execPGCommand(&pg_conn, pg_conn_str, inc_epoch_str);

        int retry = 0;
        bool ingest_gc = false;
        bool pg_error = false;
        while (!ingest_gc && retry < 5) {
            // roll back if previous iteration errored out
            if (PQstatus(pg_conn) == CONNECTION_OK && pg_error) {
                PGresult* rb_res = PQexec(pg_conn, "ROLLBACK;");
                if (PQresultStatus(rb_res) != PGRES_COMMAND_OK) {
                    std::cerr << "Rollback failed: " << PQresultErrorMessage(rb_res) << "\n";
                    PQclear(rb_res);

                    while ((rb_res = PQgetResult(pg_conn)) != NULL) {
                        PQclear(rb_res);
                    }
                    
                    PQreset(pg_conn);
                }
                else {
                    PQclear(rb_res);

                    while ((rb_res = PQgetResult(pg_conn)) != NULL) {
                        PQclear(rb_res);
                    }
                }

                pg_error = false;
            }

            if (!PQsendQuery(pg_conn, ingest_str.c_str())) {
                std::cerr << "Error sending query to the catalog instance\n";
                if (PQstatus(pg_conn) != CONNECTION_OK) {
                    std::cerr << "Error: Catalog connection failure.\n";
                    PQfinish(pg_conn);
                    pg_conn = PQconnectdb(pg_conn_str.c_str());
                }
                retry++;
            }
            else {
                std::unique_ptr<GCTask> gc_task = std::make_unique<GCTask>();
                ingest_gc = true;
                // begin transaction
                PGresult* res = PQgetResult(pg_conn);
                if (res == nullptr) {
                    pg_error = true;
                    continue;
                }
                ingest_gc = ingest_gc && (PQresultStatus(res) == PGRES_COMMAND_OK);
                pg_error = !ingest_gc;
                PQclear(res);                
                
                // add edges
                res = PQgetResult(pg_conn);
                if (res == nullptr) {
                    pg_error = true;
                    continue;
                }
                ingest_gc = ingest_gc && (PQresultStatus(res) == PGRES_TUPLES_OK);
                if (ingest_gc) {
                    int src_ent_id_fnum = PQfnumber(res, "src_ent_id");
                    int src_vid_fnum = PQfnumber(res, "src_vid");
                    int src_obj_id_fnum = PQfnumber(res, "src_obj_id");
                    int dest_ent_id_fnum = PQfnumber(res, "dest_ent_id");
                    int dest_vid_fnum = PQfnumber(res, "dest_vid");
                    int dest_obj_id_fnum = PQfnumber(res, "dest_obj_id");
                    int log_count_fnum = PQfnumber(res, "log_count");
                    
                    int num_tuples = PQntuples(res);
                    gc_task->add_edges_.reserve(num_tuples);
                    for (int i = 0; i < num_tuples; i++) {
                        ExtEdge add_edge;
                        add_edge.src_.ent_id_ =  std::stol(PQgetvalue(res, i, src_ent_id_fnum));
                        add_edge.src_.vid_ =  std::stol(PQgetvalue(res, i, src_vid_fnum));
                        add_edge.src_.obj_id_ =  std::stoll(PQgetvalue(res, i, src_obj_id_fnum));
                        add_edge.dest_.ent_id_ =  std::stol(PQgetvalue(res, i, dest_ent_id_fnum));
                        add_edge.dest_.vid_ =  std::stol(PQgetvalue(res, i, dest_vid_fnum));
                        add_edge.dest_.obj_id_ =  std::stoll(PQgetvalue(res, i, dest_obj_id_fnum));
                        int log_count = std::stol(PQgetvalue(res, i, log_count_fnum));
                        gc_task->add_edges_.emplace_back(add_edge, log_count);
                    }
                }
                else {
                   pg_error = true; 
                }
                PQclear(res);

                // delete edges
                res = PQgetResult(pg_conn);
                if (res == nullptr) {
                    pg_error = true;
                    continue;
                }
                ingest_gc = ingest_gc && (PQresultStatus(res) == PGRES_TUPLES_OK);
                if (ingest_gc) {
                    int src_ent_id_fnum = PQfnumber(res, "src_ent_id");
                    int src_vid_fnum = PQfnumber(res, "src_vid");
                    int src_obj_id_fnum = PQfnumber(res, "src_obj_id");
                    int dest_ent_id_fnum = PQfnumber(res, "dest_ent_id");
                    int dest_vid_fnum = PQfnumber(res, "dest_vid");
                    int dest_obj_id_fnum = PQfnumber(res, "dest_obj_id");
                    int log_count_fnum = PQfnumber(res, "log_count");

                    int num_tuples = PQntuples(res);
                    gc_task->delete_edges_.reserve(num_tuples);
                    for (int i = 0; i < num_tuples; i++) {
                        ExtEdge delete_edge;
                        delete_edge.src_.ent_id_ =  std::stol(PQgetvalue(res, i, src_ent_id_fnum));
                        delete_edge.src_.vid_ =  std::stol( PQgetvalue(res, i, src_vid_fnum));
                        delete_edge.src_.obj_id_ =  std::stoll(PQgetvalue(res, i, src_obj_id_fnum));
                        delete_edge.dest_.ent_id_ =  std::stol(PQgetvalue(res, i, dest_ent_id_fnum));
                        delete_edge.dest_.vid_ =  std::stol(PQgetvalue(res, i, dest_vid_fnum));
                        delete_edge.dest_.obj_id_ =  std::stoll(PQgetvalue(res, i, dest_obj_id_fnum));
                        int log_count = std::stol(PQgetvalue(res, i, log_count_fnum));
                        gc_task->delete_edges_.emplace_back(delete_edge, log_count);
                    }
                }
                else {
                   pg_error = true; 
                }
                PQclear(res);

                // epoch id and current time
                res = PQgetResult(pg_conn);
                if (res == nullptr) {
                    pg_error = true;
                    continue;
                }
                ingest_gc = ingest_gc && (PQresultStatus(res) == PGRES_TUPLES_OK);
                if (ingest_gc) {
                    int epoch_id_fnum = PQfnumber(res, "epoch_id");
                    int cur_time_fnum = PQfnumber(res, "cur_time");
                    for (int i = 0; i < PQntuples(res); i++) {
                        gc_task->epoch_id_ =  std::stoll(PQgetvalue(res, i, epoch_id_fnum));
                        gc_task->delete_time_ =  PQgetvalue(res, i, cur_time_fnum);
                    }
                }
                else {
                   pg_error = true; 
                }
                PQclear(res);
                
                // commit
                res = PQgetResult(pg_conn);
                if (res == nullptr) {
                    pg_error = true;
                    continue;
                }
                ingest_gc = ingest_gc && (PQresultStatus(res) == PGRES_COMMAND_OK);
                pg_error = !ingest_gc;
                PQclear(res);

                // clear out the rest of result set
                while ((res = PQgetResult(pg_conn)) != NULL) {
                    PQclear(res);
                }

                if (pg_error) {
                    continue;
                }

                if (ingest_gc && !(gc_task->add_edges_.empty() && gc_task->delete_edges_.empty())) {
                    GCTask* task = gc_task.release();
                    // updating graph store cannot be pipelined due to recovery purposes...
                    task->new_ext_vertices_ = gc_graph_.updateGraphStore(task->add_edges_, task->delete_edges_, task->epoch_id_, task->delete_time_);
                    boost::asio::post(gc_pipeline_[0]->workers_, [this, task](){ handleGCTask(task); });
                }
            }
        }

        std::this_thread::sleep_for(epoch_period);
    }

    PQfinish(pg_conn);

}

void GCManager::clearCatalogEdges(GCTask* gc_task) {
    const std::string& pg_conn_str = Config::getPGConnString();
    PGconn* pg_conn = PGUtil::connectPG(pg_conn_str);

    const EpochId epoch_id = gc_task->epoch_id_;

    std::string command_str;
    command_str.reserve(256);
    fmt::format_to(
        std::back_inserter(command_str),
        "BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;"
        "DELETE FROM ercat.AddEdges WHERE epoch_id < {} - 1;"
        "DELETE FROM ercat.DeleteEdges WHERE epoch_id < {} - 1 "
        "AND delete_time < '{}';"
        "COMMIT;",
        epoch_id,
        epoch_id,
        gc_task->delete_time_
    );

    gc_task->valid_ = gc_task->valid_ && PGUtil::execPGCommand(&pg_conn, pg_conn_str, command_str);
    PQfinish(pg_conn);
}

void GCManager::rcCommit(GCTask* gc_task) {
    // mapping from relation id to unreachable entities, which could be source.
    boost::unordered_flat_map<RelId, std::vector<ExtVertex>*> rel_src_ent_map;
     // mapping from relation id to unreachable entities, which could be destination.
    boost::unordered_flat_map<RelId, std::vector<ExtVertex>*> rel_dest_ent_map;
    // buffer for holding all vectors of extvertex constructed
    std::vector<std::unique_ptr<std::vector<ExtVertex>>> vectors;
    // list of ent_ids present, used together with ent_id_boudns for grouping vertices 
    // by ent_id. Used for bulk delete of entities
    std::vector<EntId> ent_ids;
    // boundaries used for grouping ext vertices by ent_id
    std::vector<std::pair<size_t,size_t>> ent_id_bounds;

    const auto& src_ent_rel_map = CatCache::getInstance().srcEntRelMap();
    const auto& dest_ent_rel_map = CatCache::getInstance().destEntRelMap();
    // intialize a bunch of data structures that are used later
    std::vector<ExtVertex>& unreachable_vertices =  gc_task->unreachable_vertices_;
    for (size_t i = 0; i < unreachable_vertices.size(); i++) {
        ExtVertex ext_vertex = unreachable_vertices[i];
        // init src_ent_rel_map
        if (src_ent_rel_map.contains(ext_vertex.ent_id_)) {
            const std::vector<RelId>* src_rel_ids = src_ent_rel_map.at(ext_vertex.ent_id_);
            for (const auto& rel_id : *src_rel_ids) {
                if (!rel_src_ent_map.contains(rel_id)) {
                    vectors.push_back(std::make_unique<std::vector<ExtVertex>>());
                    rel_src_ent_map.emplace(rel_id, vectors.back().get());
                }
                rel_src_ent_map.at(rel_id)->push_back(ext_vertex);
            }
        }
        // init dest_ent_rel_map
        if (dest_ent_rel_map.contains(ext_vertex.ent_id_)) {
            const std::vector<RelId>* dest_rel_ids = dest_ent_rel_map.at(ext_vertex.ent_id_);
            for (const auto& rel_id : *dest_rel_ids) {
                if (!rel_dest_ent_map.contains(rel_id)) {
                    vectors.push_back(std::make_unique<std::vector<ExtVertex>>());
                    rel_dest_ent_map.emplace(rel_id, vectors.back().get());
                }
                rel_dest_ent_map.at(rel_id)->push_back(ext_vertex);
            }
        }

        if (ent_ids.empty()) {
            ent_ids.emplace_back(ext_vertex.ent_id_);
            ent_id_bounds.emplace_back(0,1);
        }
        // This is possible because the external vertices is sorted by ent_id, followed by obj_id and vid
        if (ent_ids.back() != ext_vertex.ent_id_) {
            ent_id_bounds.back().second = i;
            ent_ids.emplace_back(ext_vertex.ent_id_);
            ent_id_bounds.emplace_back(i,i+1);
        }
    }

    if (!ent_id_bounds.empty()) {
        ent_id_bounds.back().second = unreachable_vertices.size();
    }

    const std::string& pg_conn_str = Config::getPGConnString();
    PGconn* pg_conn = PGUtil::connectPG(pg_conn_str);

    // delete all the relations where the unreachable entity is the source
    // TODO group small delete statements together
    for (auto& rel_src_ent : rel_src_ent_map) {
        RelId rel_id = rel_src_ent.first;
        std::vector<ercat::ExtVertex>& ext_vertices = *rel_src_ent.second;
        // approx tuple size to preallocate the query string for faster construction
        size_t tuple_size = std::to_string(ext_vertices[0].ent_id_).size()
                + std::to_string(ext_vertices[0].vid_).size()
                + std::to_string(ext_vertices[0].obj_id_).size() + 3;
        std::string command_str;
        command_str.reserve(300 + ext_vertices.size() * tuple_size);
        command_str += "WITH temp AS ("
                        "SELECT * FROM "
                        "unnest(ARRAY[";
        
        for (size_t i = 0; i < ext_vertices.size(); i++) {
            command_str +=  std::to_string(ext_vertices[i].ent_id_);
            if (i < ext_vertices.size() - 1) {
                command_str += ",";
            }
        }
        command_str += "], ARRAY[";
        
        for (size_t i = 0; i < ext_vertices.size(); i++) {
            command_str += std::to_string(ext_vertices[i].vid_);
            if (i < ext_vertices.size() - 1) {
                command_str += ",";
            }
        }
        command_str += "], ARRAY[";

        for (size_t i = 0; i < ext_vertices.size(); i++) {
            command_str += std::to_string(ext_vertices[i].obj_id_);
            if (i < ext_vertices.size() - 1) {
                command_str += ",";
            }
        }
        command_str += "]) AS temp_table (ent_id, vid, obj_id)) "
            "DELETE FROM rel";
        command_str += std::to_string(rel_id);
        command_str += " AS r USING temp AS t where r.src_ent_id = t.ent_id "
            "AND r.src_vid = t.vid AND r.src_obj_id = t.obj_id;";
        gc_task->valid_ = gc_task->valid_ && PGUtil::execPGCommand(&pg_conn, pg_conn_str, command_str);
    }

    // delete all the relations where the unreachable entity is the destination
    // TODO group small delete statements together
    for (auto& rel_dest_ent : rel_dest_ent_map) {
        RelId rel_id = rel_dest_ent.first;
        std::vector<ercat::ExtVertex>& ext_vertices = *rel_dest_ent.second;
        size_t tuple_size = std::to_string(ext_vertices[0].ent_id_).size()
                + std::to_string(ext_vertices[0].vid_).size()
                + std::to_string(ext_vertices[0].obj_id_).size() + 3;
        std::string command_str;
        command_str.reserve(300 + ext_vertices.size() * tuple_size);
        command_str += "WITH temp AS ("
                        "SELECT * FROM "
                        "unnest(ARRAY[";
        for (size_t i = 0; i < ext_vertices.size(); i++) {
            command_str +=  std::to_string(ext_vertices[i].ent_id_);
            if (i < ext_vertices.size() - 1) {
                command_str += ",";
            }
        }

        command_str += "], ARRAY[";
        for (size_t i = 0; i < ext_vertices.size(); i++) {
            command_str += std::to_string(ext_vertices[i].vid_);
            if (i < ext_vertices.size() - 1) {
                command_str += ",";
            }
        }

        command_str += "], ARRAY[";
        for (size_t i = 0; i < ext_vertices.size(); i++) {
            command_str += std::to_string(ext_vertices[i].obj_id_);
            if (i < ext_vertices.size() - 1) {
                command_str += ",";
            }
        }
        command_str += "]) AS temp_table (ent_id, vid, obj_id)) "
            "DELETE FROM rel";
        command_str += std::to_string(rel_id);
        command_str += " AS r USING temp AS t where r.dest_ent_id = t.ent_id "
            "AND r.dest_vid = t.vid AND r.dest_obj_id = t.obj_id;";
        gc_task->valid_ = gc_task->valid_ && PGUtil::execPGCommand(&pg_conn, pg_conn_str, command_str);
    }

    // delete the entities themselves from the catalog
    for (size_t i = 0; i < ent_ids.size(); i++) {
        std::string command_str;
        ExtVertex first_tuple = unreachable_vertices[ent_id_bounds[i].first];
        size_t tuple_size = std::to_string(first_tuple.obj_id_).size() + std::to_string(first_tuple.vid_).size() + 2;
        command_str += "WITH temp AS (SELECT * FROM unnest(ARRAY[";
        for (size_t j = ent_id_bounds[i].first; j < ent_id_bounds[i].second; j++) {
            command_str += std::to_string(unreachable_vertices[j].obj_id_);
            if (j < ent_id_bounds[i].second - 1) {
                command_str += ",";
            }
        }
        command_str += "], ARRAY[";
        for (size_t j = ent_id_bounds[i].first; j < ent_id_bounds[i].second; j++) {
            command_str += std::to_string(unreachable_vertices[j].vid_);
            if (j < ent_id_bounds[i].second - 1) {
                command_str +=  ",";
            }
        }
        command_str += "]) AS temp_table (obj_id, vid)) DELETE FROM ent";
        command_str += std::to_string(ent_ids[i]);
        command_str += " AS e USING temp AS t where e.obj_id = t.obj_id AND e.vid = t.vid;";
        gc_task->valid_ = gc_task->valid_ && PGUtil::execPGCommand(&pg_conn, pg_conn_str, command_str);
    }

    // check for the entities that are filelists and update the version chains
    std::vector<std::pair<ExtVertex, ExtVertex>> vid_ranges;
    vid_ranges.reserve(unreachable_vertices.size());
    const auto& file_lists = CatCache::getInstance().fileLists();
    for (const auto& ext_vertex : unreachable_vertices) {
        if (file_lists.at(ext_vertex.ent_id_)) {
            vid_ranges.emplace_back(version_map_.remove(ext_vertex));
        }
    }
    std::vector<std::pair<ExtVertex, ExtVertex>> merged_vid_ranges;
    if (!vid_ranges.empty()) {
        merged_vid_ranges.push_back(vid_ranges[0]);
    }
    for (const auto& vid_range : vid_ranges) {
        // first filter out invalid entries
        if (vid_range.first.vid_ != VersionChain::invalid_vid_ ||
                vid_range.second.vid_ != VersionChain::invalid_vid_) {
            // if ent_id and obj_id match and vid ranges overlap, merge the ranges
            ExtVertex& prev_range_end = merged_vid_ranges.back().second;
            const ExtVertex& next_range_begin = vid_range.first;
            if (prev_range_end.ent_id_ == next_range_begin.ent_id_ && 
                    prev_range_end.obj_id_ == next_range_begin.obj_id_ &&
                    next_range_begin.vid_ < prev_range_end.vid_) {
                prev_range_end = vid_range.second;
            }
            // otherwise insert another range
            else {
                merged_vid_ranges.push_back(vid_range);
            }
        }
    }

    // Given the ranges, move the files that are no longer accessible to deleted files in the catalog
    for (const auto& vid_range : merged_vid_ranges) {
        ExtVertex range_start = vid_range.first;
        ExtVertex range_end = vid_range.second;
        FileListId file_list_id(range_start.ent_id_, range_start.obj_id_);
        if (range_end.vid_ - range_start.vid_ > 1) {
            std::string command_str;
            command_str.reserve(400);
            fmt::format_to(
                std::back_inserter(command_str),
                "BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;"
                "WITH temp AS (DELETE FROM ercat.Files_{} f WHERE f.ent_id = {} AND f.obj_id = {} "
                "AND f.start_vid > {} AND f.end_vid < {} + 1 "
                "RETURNING f.file_path) INSERT INTO ercat.DeleteFiles (file_path) SELECT file_path FROM temp;"
                "COMMIT;",
                hash_value(file_list_id) % 1024,
                range_start.ent_id_,
                range_start.obj_id_,
                range_start.vid_,
                range_end.vid_
            );
            gc_task->valid_ = gc_task->valid_ && PGUtil::execPGCommand(&pg_conn, pg_conn_str, command_str);
        }
    }
    
    PQfinish(pg_conn);

    file_manager_.ingestFileTasks();

}

void inline GCManager::pipeGCTask(GCTask* gc_task) {
    // if task is invalid (most likely due to Metadata Store failure), halt
    if (!gc_task->valid_) {
        delete gc_task;
        return;
    }
    gc_task->task_state_++;
    boost::asio::post(gc_pipeline_[gc_task->task_state_]->workers_, [this, gc_task](){ handleGCTask(gc_task); });
}

void GCManager::handleGCTask(GCTask* gc_task) {
    switch (gc_task->task_state_) {
        case 0: {
            gc_task->merged_new_ext_vertices_ = GCGraph::mergeVectors(gc_task->new_ext_vertices_, true, true);
            gc_task->new_ext_vertices_.clear();
            const auto& file_lists = CatCache::getInstance().fileLists();
            // update the version chains for new versions
            for (const auto& ext_vertex : gc_task->merged_new_ext_vertices_) {
                if (file_lists.at(ext_vertex.ent_id_)) {
                    version_map_.insert(ext_vertex);
                }
            }
            pipeGCTask(gc_task);
            }
            break;
        case 1:
            clearCatalogEdges(gc_task);
            pipeGCTask(gc_task);
            break;
        case 2:
            gc_task->new_vertices_ = gc_graph_.updateVertexMaps(gc_task->merged_new_ext_vertices_);
            pipeGCTask(gc_task);
            break;
        case 3:
            gc_task->delete_vertices_ = gc_graph_.updateGraphEngine(gc_task->add_edges_, gc_task->delete_edges_,
                    gc_task->new_vertices_);
            pipeGCTask(gc_task);
            break;
        case 4:
            gc_task->unreachable_edges_ = gc_graph_.execRC(gc_task->delete_vertices_, gc_task->unreachable_vertices_);
            pipeGCTask(gc_task);
            break;
        case 5:
            // perform rc commit, deleting all unreachable objects in the catalog + all the relations they are part of.
            // In addition, update the version chain and move corresponding files in the catalog to deleted files
            rcCommit(gc_task);
            pipeGCTask(gc_task);
            break;
        case 6:
            gc_graph_.postRCCommit(gc_task->unreachable_vertices_, gc_task->delete_edges_, gc_task->unreachable_edges_);
            delete gc_task;
            break;
        default:
            break;
    }
}


}
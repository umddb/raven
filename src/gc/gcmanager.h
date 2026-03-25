#pragma once

#include <utility>

#include "postgresql/libpq-fe.h"

#include "gc/filemanager.h"
#include "gc/gcgraph.h"
#include "gc/versionmap.h"
#include "utils/pgutil.h"
#include "utils/threadpool.h"

namespace ercat {

class GCManager {
public:
    struct GCTask {
        static constexpr int max_task_state_ = 6;

        GCTask() : task_state_(0), valid_(true) { }
        ~GCTask() { }
        // epoch id of the gc task
        EpochId epoch_id_;
        // deletion time stamp of gc task
        std::string delete_time_;
        // may use enum in the future, but int makes it very simple.
        int task_state_;
        // whether the last stage succeeded
        bool valid_;
        // initial set of edges added
        std::vector<std::pair<ExtEdge, int>> add_edges_;
        // initial set of edges deleted
        std::vector<std::pair<ExtEdge, int>> delete_edges_;
        // new external vertices added
        std::vector<std::vector<ExtVertex>> new_ext_vertices_;
        // flattened version of nex_ext_vertices
        std::vector<ExtVertex> merged_new_ext_vertices_;
        // new vertices
        std::vector<Vertex> new_vertices_;
        // vertices with ref count of 0 as a result of delete_edges_
        std::vector<Vertex> delete_vertices_;
        // the total set of unreachable vertices
        std::vector<ExtVertex> unreachable_vertices_;
        // the total set of unreachable edges
        std::vector<ExtEdge> unreachable_edges_;
    };

    static void init();
    static GCManager& getInstance();
    

    ~GCManager();
    // start the background threads (ingestors & file cleaners)
    void start();
    void execGCTask(GCTask* gc_task);
    void shutDown();

private:
    static GCManager& getInstanceImpl();

    GCManager();
    void ingestGC(std::atomic<bool>& stop);
    void clearCatalogEdges(GCTask* gc_task);
    void rcCommit(GCTask* gc_task);
    void pipeGCTask(GCTask* gc_task);
    void handleGCTask(GCTask* gc_task);

    // Ingestor for ingesting updates from the catalog.
    std::unique_ptr<ThreadPool> gc_ingestor_;
    // main gc pipeline with gc task passed from one thread pool to next. 
    // Each threadpool is single threaded, but they may utilize the thread pool 
    // in gcgraph for parallel execution. May want to change to custom threads
    // with concurrent queues in the future, but it may not be necessary. 
    std::vector<std::unique_ptr<ThreadPool>> gc_pipeline_;
    // reference to GCGraph instance
    GCGraph& gc_graph_;
    FileManager& file_manager_;
    VersionMap version_map_;
    bool shut_down_;

};




}
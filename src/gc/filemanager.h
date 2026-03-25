#pragma once

#include <utility>
#include <fstream>
#include <random>

#include "aws/core/Aws.h"
#include "aws/s3/S3Client.h"
#include "postgresql/libpq-fe.h"

#include "gc/gcgraph.h"
#include "utils/pgutil.h"
#include "utils/paddedmutex.h"
#include "utils/threadpool.h"

namespace ercat {

class FileManager {
public:
    struct PartitionEntry {
        PartitionEntry() { }
        PartitionEntry(int partition_id, size_t start_idx, size_t end_idx) :
                partition_id_(partition_id), start_idx_(start_idx), end_idx_(end_idx) { }
        
        int partition_id_;
        // start idx in the file paths vector for the given partition
        size_t start_idx_;
        // end idx (not inclusive) in the file paths vector for the partition
        size_t end_idx_;
    };

    struct FileTask {
        FileTask() { }
        ~FileTask() { }

        // list of files to delete
        std::vector<std::string> file_paths_;
        // index for grouping files by partition id
        std::vector<PartitionEntry> partition_idx_;
    };

    static void init();
    static FileManager& getInstance();
    
    ~FileManager(); 
    void ingestFileTasks();
    void shutDown();

private:
    enum class FileType { LOCAL,
                          HDFS,
                          S3 };

    static FileManager& getInstanceImpl();
    static int myIngestorId();
    static int myExecutorId();
    static std::mt19937& getThreadLocalRng();

    FileManager();

    void ingestFileTasksImpl();
    void execFileTask(FileTask* file_task);
    std::vector<bool> removeFiles(std::vector<std::string>& file_paths);
    std::vector<bool> removeLocalFiles(std::vector<std::string>& file_paths);
    std::vector<bool> removeS3Objects(std::vector<std::string>& obj_paths);
    void reportMetrics(size_t count, std::time_t start_c, std::time_t end_c, int64_t start_ms, int64_t end_ms);

    // Ingestor for ingesting file tasks from the catalog.
    std::unique_ptr<ThreadPool> file_task_ingestors_;
    // Executors that actually delete the files & update the catalog
    std::unique_ptr<ThreadPool> file_task_executors_;
    // for exporting metrics if it is set
    std::unique_ptr<ThreadPool> metrics_reporter_;
    // vector of pg connections for ingestors
    std::vector<PGconn*> ingestor_pg_conn_;
    // vector of pg connections for executors
    std::vector<PGconn*> executor_pg_conn_;
    // vector of mutexes for executor pg conns
    PaddedMutex* executor_mtx_;
    std::string pg_conn_str_;
    // AWS SDK options for S3
    Aws::SDKOptions aws_sdk_options_;
    // S3 client
    std::unique_ptr<Aws::S3::S3Client> s3_client_;
    // the output file to which metrics are streamed
    std::ofstream metrics_output_;
    // whether to report metrics
    bool report_metrics_;

    int num_files_removed_;
    
    bool shut_down_;
};

}
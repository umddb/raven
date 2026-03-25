#include <chrono>
#include <filesystem>

#include "aws/core/auth/AWSCredentials.h"
#include "aws/s3/S3Client.h"
#include "aws/s3/model/DeleteObjectsRequest.h"
#include "aws/s3/model/ObjectIdentifier.h"
#include "boost/asio/post.hpp"
#include "boost/unordered/unordered_flat_map.hpp"
#include "fmt/format.h"

#include "gc/filemanager.h"
#include "utils/config.h"

namespace ercat {

void FileManager::init() {
    getInstanceImpl();
}

FileManager& FileManager::getInstance() {
    return getInstanceImpl();
}

FileManager::~FileManager() {
    shutDown();
    delete[] executor_mtx_;
}

void FileManager::ingestFileTasks() {
    for (int i = 0; i < file_task_ingestors_->num_workers_; i++) {
        boost::asio::post(file_task_ingestors_->workers_, [ this ](){ 
            ingestFileTasksImpl();     
        });
    }    
}

void FileManager::shutDown() {
    if (!shut_down_) {
        file_task_ingestors_->shutDown();
        file_task_executors_->shutDown();
        metrics_reporter_->shutDown();
        for (auto pg_conn : ingestor_pg_conn_) {
            PQfinish(pg_conn);
        }
        for (auto pg_conn : executor_pg_conn_) {
            PQfinish(pg_conn);
        }
        s3_client_.reset();
        Aws::ShutdownAPI(aws_sdk_options_);
        if (metrics_output_.is_open()) {
            metrics_output_.flush();
            metrics_output_.close();
        }
        shut_down_ = true;
    }
}

FileManager& FileManager::getInstanceImpl() {
    static FileManager instance;
    return instance;
}

int FileManager::myIngestorId() {
    static std::atomic<int> ingestor_counter = 0;
    // each thread gets a unique ctx id if not initialized
    static thread_local int my_ingestor_id = ingestor_counter.fetch_add(1);
    return my_ingestor_id;
}

int FileManager::myExecutorId() {
    static std::atomic<int> executor_counter = 0;
    // each thread gets a unique ctx id if not initialized
    static thread_local int my_executor_id = executor_counter.fetch_add(1);
    return my_executor_id;
}

std::mt19937& FileManager::getThreadLocalRng() {
    // These are initialized exactly ONCE per thread, the first time this function is called.
    thread_local std::random_device rd;
    thread_local std::mt19937 gen(rd());
    
    return gen;
}

FileManager::FileManager() : report_metrics_(false), num_files_removed_(0), shut_down_(false) {
    const Config& config = Config::getInstance();
    pg_conn_str_ = Config::getPGConnString();
    int num_ingestors = std::stoi(config.get("gc.num_file_task_ingestors"));
    int num_executors = std::stoi(config.get("gc.num_file_task_executors"));
    if (config.contains("gc.metrics_output")) {
        report_metrics_ = true;
    }

    file_task_ingestors_ = std::make_unique<ThreadPool>(num_ingestors);
    file_task_executors_ = std::make_unique<ThreadPool>(num_executors);
    metrics_reporter_ = std::make_unique<ThreadPool>(1);
    
    for (int i = 0; i < num_ingestors; i++) {
        ingestor_pg_conn_.push_back(PGUtil::connectPG(pg_conn_str_));
    }
    
    for (int i = 0; i < num_executors/4; i++) {
        executor_pg_conn_.push_back(PGUtil::connectPG(pg_conn_str_));
    }
    executor_mtx_ = new PaddedMutex[num_executors/4];
    Aws::InitAPI(aws_sdk_options_);

    // if the S3 credentials are in the config, initialize s3 client 
    if (config.contains("aws.access") && config.contains("aws.secret") && config.contains("aws.region")) {
        Aws::String access_key = config.get("aws.access");
        Aws::String secret_key = config.get("aws.secret");
        Aws::Auth::AWSCredentials credentials(access_key, secret_key);
        Aws::Client::ClientConfiguration client_config;
        client_config.region = config.get("aws.region");
        s3_client_ = std::make_unique<Aws::S3::S3Client>(credentials, nullptr, client_config);
    }

}

void FileManager::ingestFileTasksImpl() {
    int ingestor_id = myIngestorId();
    int num_ingestors = file_task_ingestors_->num_workers_;
    PGconn* pg_conn = ingestor_pg_conn_[ingestor_id];

    FileTask* file_task = new FileTask();
    std::string ingest_query;
    for (int i = 0; i < 32; i++) {
        if (i % num_ingestors == ingestor_id) {
            ingest_query.clear();
            if (file_task->partition_idx_.empty()) {
                file_task->partition_idx_.emplace_back(PartitionEntry(i, 0, 0));
            }
            else {
                PartitionEntry& prev_entry = file_task->partition_idx_.back();
                size_t end_idx = file_task->file_paths_.size();
                prev_entry.end_idx_ = end_idx;
                file_task->partition_idx_.emplace_back(PartitionEntry(i, end_idx, end_idx));
            }

            fmt::format_to(
                std::back_inserter(ingest_query),
                "BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;"
                "WITH temp AS (SELECT file_path, COUNT(*) AS del_count FROM ercat.DeleteFiles_{0} GROUP BY file_path), "
                "temp2 AS (SELECT a.file_path, t.del_count, COUNT(*) AS add_count FROM ercat.AddFiles_{0} a "
                    "INNER JOIN temp t ON a.file_path = t.file_path GROUP BY a.file_path, t.del_count) "
                "SELECT file_path FROM temp2 WHERE del_count = add_count;"
                "COMMIT;",
                i
            );

            bool res_valid = false;
            int retry = 0;
            bool pg_error = false;
            while (!res_valid && retry < 5) {
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
  
                if (!PQsendQuery(pg_conn, ingest_query.c_str())) {
                    std::cerr << "Error sending query to the catalog instance\n";
                    if (PQstatus(pg_conn) != CONNECTION_OK) {
                        std::cerr << "Error: Catalog connection failure.\n";
                        PQfinish(pg_conn);
                        pg_conn = PQconnectdb(pg_conn_str_.c_str());
                        ingestor_pg_conn_[ingestor_id] = pg_conn;
                    }
                    retry++;
                }
                else {
                    // begin transaction    
                    PGresult* res = PQgetResult(pg_conn);
                    if (res == nullptr) {
                        pg_error = true;
                        continue;
                    }

                    res_valid = (PQresultStatus(res) == PGRES_COMMAND_OK);
                    pg_error = !res_valid;
                    PQclear(res);
            
                    // select query
                    res = PQgetResult(pg_conn);
                    if (res == nullptr) {
                        pg_error = true;
                        continue;
                    }

                    res_valid = res_valid && (PQresultStatus(res) == PGRES_TUPLES_OK);
                    if (res_valid) {
                        int file_path_fnum = PQfnumber(res, "file_path");
                        for (int j = 0; j < PQntuples(res); j++) {
                            file_task->file_paths_.emplace_back(std::string(PQgetvalue(res, j, file_path_fnum)));
                            // file task is full
                            if (file_task->file_paths_.size() >= 1000) {
                                file_task->partition_idx_.back().end_idx_ = file_task->file_paths_.size();
                                boost::asio::post(file_task_executors_->workers_, [ this, file_task ](){ 
                                    execFileTask(file_task);     
                                });
                                file_task = new FileTask();
                                file_task->partition_idx_.emplace_back(PartitionEntry(i,0,0));
                            }  
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

                    res_valid = res_valid && (PQresultStatus(res) == PGRES_COMMAND_OK);
                    pg_error = !res_valid;
                    PQclear(res);

                    // clear out the rest of result set
                    while ((res = PQgetResult(pg_conn)) != NULL) {
                        PQclear(res);
                    }

                    if (pg_error) {
                        continue;
                    }

                    break;
                }
            }
        }
    }

    // start filetask only if there are files to delete 
    if (!file_task->file_paths_.empty()) {
        file_task->partition_idx_.back().end_idx_ = file_task->file_paths_.size();
        boost::asio::post(file_task_executors_->workers_, [ this, file_task ](){ 
                execFileTask(file_task);     
        });
    }
    else {
        delete file_task;
    }
    
}

void FileManager::execFileTask(FileTask* file_task) {
    int pg_conn_id = myExecutorId() % executor_pg_conn_.size();
    std::vector<std::string>& file_paths = file_task->file_paths_;
    std::vector<PartitionEntry>& partition_idx = file_task->partition_idx_;

    auto start = std::chrono::system_clock::now();

    std::vector<bool> removed = removeFiles(file_paths);

    size_t total_num_files_removed = 0;
    std::string command;
    for (const auto& partition_entry : partition_idx) {
        size_t start_idx = partition_entry.start_idx_;
        size_t end_idx = partition_entry.end_idx_;
        if (start_idx == end_idx) {
            continue;
        }

        size_t num_files_removed = 0;
        command.clear();
        command.append("WITH temp(file_path) AS (VALUES ");
        for (size_t j = start_idx; j < end_idx; j++) {
            if (removed[j]) {
                command.append("('");
                command.append(file_paths[j]);
                command.append("'),");
                num_files_removed++;
            }
        }

        if (num_files_removed > 0) {
            command.pop_back();
            fmt::format_to(
                std::back_inserter(command),
                "), temp2 AS (DELETE FROM ercat.AddFiles_{0} a USING temp WHERE a.file_path = temp.file_path) "
                "DELETE FROM ercat.DeleteFiles_{0} d USING temp WHERE d.file_path = temp.file_path",
                partition_entry.partition_id_
            );

            std::unique_lock<std::mutex> lock(executor_mtx_[pg_conn_id].mtx_);
            PGUtil::execPGCommand(&executor_pg_conn_[pg_conn_id], pg_conn_str_, command);
            lock.unlock();
        }

        total_num_files_removed += num_files_removed;
    }

    if (report_metrics_) {
        auto end = std::chrono::system_clock::now();
        std::time_t start_c = std::chrono::system_clock::to_time_t(start);
        std::time_t end_c = std::chrono::system_clock::to_time_t(end);
        int64_t start_ms = (std::chrono::duration_cast<std::chrono::milliseconds>(start.time_since_epoch()) % 1000).count();
        int64_t end_ms = (std::chrono::duration_cast<std::chrono::milliseconds>(end.time_since_epoch()) % 1000).count();
        
        boost::asio::post(metrics_reporter_->workers_, [ this, total_num_files_removed, start_c, end_c, start_ms, end_ms ](){ 
            reportMetrics(total_num_files_removed, start_c, end_c, start_ms, end_ms);
        });
    }
    
    delete file_task;

}

std::vector<bool> FileManager::removeFiles(std::vector<std::string>& file_paths) {
    // list of paths partitioned by file types
    std::vector<std::pair<FileType, size_t>> partition_idx;
    std::vector<std::string> local_file_paths;
    std::vector<std::string> s3_obj_paths;
    // a little wasteful, but preallocate for speed
    partition_idx.reserve(file_paths.size());
    local_file_paths.reserve(file_paths.size());
    s3_obj_paths.reserve(file_paths.size());
    
    const std::string s3_prefix = "s3://";
    for (auto& file_path : file_paths) {
        if (file_path.compare(0, s3_prefix.size(), s3_prefix) == 0) {
            s3_obj_paths.emplace_back(file_path);
            partition_idx.emplace_back(FileType::S3, s3_obj_paths.size() - 1);
        }
        else {
            local_file_paths.emplace_back(file_path);
            partition_idx.emplace_back(FileType::LOCAL, local_file_paths.size() - 1);
        }
    }
    
    std::vector<bool> total_removed(file_paths.size(), false);
    std::vector<bool> s3_removed;
    // only call s3 operations if s3 client is valid
    if (s3_client_ != nullptr) {
        s3_removed = removeS3Objects(s3_obj_paths);
    }
    else {
        s3_removed.assign(s3_obj_paths.size(), false);
    }
    std::vector<bool> local_files_removed = removeLocalFiles(local_file_paths);

    for (size_t i = 0; i < partition_idx.size(); i++) {
        switch (partition_idx[i].first) {
            case FileType::S3:
                total_removed[i] = s3_removed[partition_idx[i].second];
                break;
            case FileType::LOCAL:
                total_removed[i] = local_files_removed[partition_idx[i].second];
                break;
            default:
                break;
        }
    }

    return total_removed;
}

std::vector<bool> FileManager::removeLocalFiles(std::vector<std::string>& file_paths) {
    std::vector<bool> removed(file_paths.size(), false);

    for (size_t i = 0; i < file_paths.size(); i++) {
        std::error_code ec;
        std::filesystem::remove(file_paths[i], ec);
        removed[i] = !ec;
    }
    
    return removed;   
}

std::vector<bool> FileManager::removeS3Objects(std::vector<std::string>& obj_paths) {
    std::vector<bool> removed(obj_paths.size(), true);

    if (obj_paths.empty()) {
        return removed;
    }

    // partitioned by bucket
    boost::unordered_flat_map<std::string, std::vector<Aws::String>*> partition_idx;
    // buffer of vectors for easier memory deallocation
    std::vector<std::unique_ptr<std::vector<Aws::String>>> vectors;
    // inverted index for error handling
    boost::unordered_flat_map<std::string, size_t> inverted_idx;
    
    const std::string s3_prefix = "s3://";
    for (size_t i = 0; i < obj_paths.size(); i++) {
        size_t bucket_start = s3_prefix.size();
        size_t first_slash = obj_paths[i].find('/', bucket_start);
        if (first_slash == std::string::npos) {
            continue;
        }
        
        std::string bucket = obj_paths[i].substr(bucket_start, first_slash - bucket_start);
        std::string key = obj_paths[i].substr(first_slash + 1);
        
        if (!partition_idx.contains(bucket)) {
            vectors.push_back(std::make_unique<std::vector<Aws::String>>());
            partition_idx.emplace(bucket, vectors.back().get());
        }

        // populate partition index
        partition_idx.at(bucket)->push_back(key);
        // populate inverted index
        inverted_idx.emplace(obj_paths[i], i);
    }

    std::mt19937& local_gen = getThreadLocalRng();
    for (auto& elem : partition_idx) {
        const std::string& bucket = elem.first;
        Aws::S3::Model::Delete delete_list;
        for (const auto& key : *elem.second) {
            Aws::S3::Model::ObjectIdentifier obj;
            obj.SetKey(key);
            delete_list.AddObjects(obj);
        }

        Aws::S3::Model::DeleteObjectsRequest request;
        request.SetBucket(bucket);
        request.SetDelete(delete_list);

        // --- Exponential Backoff Configuration ---
        int max_retries = 5;
        int attempt = 0;
        bool success = false;
        long base_delay_ms = 200;
        long max_delay_ms = 10000; // Cap the maximum sleep at 10 seconds

        while (!success && attempt <= max_retries) {
            auto outcome = s3_client_->DeleteObjects(request);

            if (outcome.IsSuccess()) {
                success = true; // Breaks the while loop
                
                // Note: Individual objects might still fail (e.g., permissions). 
                // We check the 'Errors' list in the response.
                const auto& errors = outcome.GetResult().GetErrors();
                std::string full_path;
                for (const auto& error : errors) {
                    full_path.clear();
                    full_path.append(s3_prefix);
                    full_path.append(bucket);
                    full_path.append("/");
                    full_path.append(error.GetKey());
                    removed[inverted_idx.at(full_path)] = false;
                }
            } 
            else {
                // Determine if the error is a rate limit throttle
                auto error_type = outcome.GetError().GetErrorType();
                auto response_code = outcome.GetError().GetResponseCode();
                
                bool is_throttled = (error_type == Aws::S3::S3Errors::SLOW_DOWN) || 
                                    (response_code == Aws::Http::HttpResponseCode::SERVICE_UNAVAILABLE);

                if (is_throttled && attempt < max_retries) {
                    attempt++;
                    
                    // Calculate exponential backoff with Full Jitter
                    long current_max_delay = std::min(max_delay_ms, base_delay_ms * (1L << attempt));
                    std::uniform_int_distribution<long> dist(0, current_max_delay);
                    long sleep_time_ms = dist(local_gen);

                    std::cerr << "S3 Throttling detected (SlowDown). Retrying batch in " 
                            << sleep_time_ms << " ms. (Attempt " << attempt << ")" << std::endl;

                    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
                } 
                else {
                    // The request failed due to a hard error (e.g. AccessDenied, NoSuchBucket) 
                    // OR we exhausted all our retries. We must record the failure and move on.
                    if (attempt >= max_retries) {
                        std::cerr << "Max retries exhausted for batch. Final error: ";
                    } else {
                        std::cerr << "Hard error deleting objects: ";
                    }
                    std::cerr << outcome.GetError().GetMessage() << std::endl;

                    std::string full_path;
                    for (const auto& key : *elem.second) {
                        full_path.clear();
                        full_path.append(s3_prefix);
                        full_path.append(bucket);
                        full_path.append("/");
                        full_path.append(key);
                        removed[inverted_idx.at(full_path)] = false;
                    }
                    break; // Break the retry loop, proceed to the next partition in the outer for-loop
                }
            }
        }
    }

    return removed;
}

void FileManager::reportMetrics(size_t count, std::time_t start_c, std::time_t end_c,
            int64_t start_ms, int64_t end_ms) {
    if (!metrics_output_.is_open()) {
        metrics_output_.open(Config::getInstance().get("gc.metrics_output"), std::ios_base::app);
    }
    
    struct tm start_parts;
    localtime_r(&start_c, &start_parts);

    struct tm end_parts;
    localtime_r(&end_c, &end_parts);

    metrics_output_ << "{\"count\": " << count 
            << ", \"start\": \"" << std::put_time(&start_parts, "%Y-%m-%d %H:%M:%S") 
            << '.' << std::setfill('0') << std::setw(3) << start_ms << "\""
            << ", \"end\": \"" << std::put_time(&end_parts, "%Y-%m-%d %H:%M:%S") 
            << '.' << std::setfill('0') << std::setw(3) << end_ms
            << "\"}\n";
    num_files_removed_ += count;
    std::cout << "Files removed so far " << num_files_removed_ << "\n";
}


}
#pragma once

#include <atomic>
#include <cstdint>
#include <shared_mutex>

#include "boost/asio/thread_pool.hpp"

namespace ercat {

// struct for ThreadPool 
struct alignas(64) ThreadPool {
    ThreadPool(int num_workers) : workers_(num_workers), num_workers_(num_workers), shut_down_(false) {
        counter_.store(0);
        stop_.store(false);
    }
    ~ThreadPool() {
        shutDown();
    }
    void shutDown() {
        if (!shut_down_) {
            stop_.store(true);
            workers_.join();
            shut_down_ = true;
        }
    }

    // thread pool for implementing reference counting
    boost::asio::thread_pool workers_;
    char padding1_[64 - sizeof(workers_)];
    // counter used for coordinating referece counting workloads between threads
    std::atomic<int> counter_;
    // stop variable used for stopping background threads
    std::atomic<bool> stop_;
    char padding2_[64 - sizeof(counter_) - sizeof(stop_)];
    // size of thread pool
    const int num_workers_;
    // whether thread pool is shut down
    bool shut_down_;
};

}
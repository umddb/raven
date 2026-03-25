#pragma once

#include <mutex>
#include <shared_mutex>

namespace ercat {

struct alignas(64) PaddedMutex  {
    std::mutex mtx_;
    char padding_[64 - sizeof(std::mutex)];
};

struct alignas(64) PaddedSharedMutex {
    std::shared_mutex mtx_;
    char padding_[64 - sizeof(std::shared_mutex)];
};

}
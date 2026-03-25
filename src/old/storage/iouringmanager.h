#pragma once

#include <string>
#include <shared_mutex>
#include <vector>

#include <liburing.h>
#include "utils/error.h"
#include "storage/storage.h"

namespace ercat {

struct alignas(64) IOURingContext {
    std::shared_mutex mtx_;
    io_uring io_uring_ring_;
};

class IOURingManager {
public:    
    static void init();
    static IOURingManager& getInstance();
    // static void registerFiles(const std::vector&<size_t> fd);
    // static void registerBuffers(const std::vector&<size_t*> buffers);

    ~IOURingManager();
    int submit(int num_ios, IOHandle** staged_ios);
    void waitOne(IOHandle* ioh, uint64_t ref_gen);

private:
    static IOURingManager& getInstanceImpl();
    // Note that the context ID for storage manager & io_uring manager are
    // independent to avoid keeping track of global state 
    static int myContextId();
    static void IoToSqe(IOHandle* ioh, io_uring_sqe* sqe);
    static void drainCQ(IOURingContext* ctx);

    IOURingManager();

    std::vector<IOURingContext> io_uring_ctx_;
};
    
}
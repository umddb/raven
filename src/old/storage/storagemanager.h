#pragma once

#include <string>
#include <vector>
#include <liburing.h>

#include "storage/storage.h"
#include "utils/config.h"
#include "utils/embeddedlist.h"

namespace ercat {

class StorageManager {
public:
    static void init();
    static StorageManager& getInstance();
    static void processCompleteIO(IOHandle* ioh, int32_t result);

    ~StorageManager();
    std::vector<iovec>& iovecs();
    IOHandle* acquireIOHandle();
    IOHandle* acquireIOHandleNB();
    void releaseIOHandle(IOHandle* ioh);
    void startReadV(IOHandle* ioh, int fd, uint32_t block_num, void** buffers, uint32_t num_blocks);
    void waitOnIOWaitRef(IOWaitRef* wref);
    bool haveStagedIO();
    void submitStagedIO();
    void writev(int fd, uint32_t block_num, void** buffers, uint32_t num_blocks, bool skip_fsync);

private:
    static StorageManager& getInstanceImpl();
    static int myContextId();
    static int buffersToIovecs(iovec* iov, void** buffers, int num_blocks);
    
    StorageManager();
    void reclaimIOHandle(IOHandle* ioh);
    void waitForFreeIOHandle();
    void waitForIO(IOHandle* ioh, uint64_t ref_gen);
    void stageIO(IOHandle* ioh, IOOp ioop);
    
    
    // io_uring manager
    IOURingManager& io_uring_mgr_;
    // pool of IO contexts, one for each worker thread.
    std::vector<IOContext> io_ctx_pool_;
    // pool of IO handles, partitioned among worker threads.
    std::vector<IOHandle> ioh_pool_;
    // pool of iovec, partitioned among worker threads.
    std::vector<iovec> iovec_pool_;
    // number of worker threads
    int num_workers_;
    // io queue depth
    int io_queue_depth_;

};

}
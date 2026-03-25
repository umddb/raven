#include <cstddef>
#include <iostream>

#include "buffer/buffermanager.h"
#include "storage/iouringmanager.h"
#include "storage/storagemanager.h"

namespace ercat {

void StorageManager::init() {
    getInstanceImpl();
}

StorageManager& StorageManager::getInstance() {
    return getInstanceImpl();
}

// pgaio_io_process_completion(PgAioHandle *ioh, int result)
void StorageManager::processCompleteIO(IOHandle* ioh, int32_t result) {
    std::unique_lock<std::mutex> lock(ioh->mtx_);
    ioh->result_ = result;
    ioh->updateState(IOHandleState::COMPLETED_IO);
    BufferManager::bufferReadvCompleteOne(ioh);
    ioh->updateState(IOHandleState::COMPLETED_SHARED);
    lock.unlock();
    ioh->cv_.notify_all();
}

// no clean up to do for now
StorageManager::~StorageManager() { }

std::vector<iovec>& StorageManager::iovecs() {
    return iovec_pool_;
}

IOHandle* StorageManager::acquireIOHandle() {
    IOHandle* ioh;

    while (true) {
        ioh = acquireIOHandleNB();
        if (ioh != nullptr) {
            return ioh;
        }
        waitForFreeIOHandle();
    }

    return ioh;
}

IOHandle* StorageManager::acquireIOHandleNB() {
    IOHandle* ioh = nullptr;
    IOContext& my_io_ctx = io_ctx_pool_[myContextId()];
    
    if (my_io_ctx.num_staged_ios_ >= 32) {
        submitStagedIO();
    }

    if (!my_io_ctx.free_ioh_.isEmpty()) {
        DListNode* head = my_io_ctx.free_ioh_.popFront();
        IOHandle* ioh = reinterpret_cast<IOHandle*>(reinterpret_cast<char*>(head) - (offsetof(IOHandle, node_)));
        ioh->updateState(IOHandleState::HANDED_OUT);
    }

    return ioh;
}

void StorageManager::releaseIOHandle(IOHandle* ioh) {
    reclaimIOHandle(ioh);
}

// mdstartreadv(PgAioHandle *ioh,
//			 SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
//			 void **buffers, BlockNumber nblocks)
// pgaio_io_start_readv(PgAioHandle *ioh,
//					 int fd, int iovcnt, uint64 offset)
void StorageManager::startReadV(IOHandle* ioh, int fd, uint32_t block_num, void** buffers,
            uint32_t num_blocks) {
    int64_t seek_pos = PAGE_SIZE * block_num;
    iovec* iov = &iovec_pool_[ioh->iovec_off_];
    
    int	iovcnt = buffersToIovecs(iov, buffers, num_blocks);
    
    ioh->op_data_.fd_ = fd;
    ioh->op_data_.offset_ = seek_pos;
    ioh->op_data_.iov_length_ = iovcnt;
    stageIO(ioh, IOOp::READV);
}

// pgaio_wref_wait(PgAioWaitRef *iow)
void StorageManager::waitOnIOWaitRef(IOWaitRef* wref) {
    waitForIO(wref->ioh_, wref->generation_);
}


bool StorageManager::haveStagedIO() {
    return (io_ctx_pool_[myContextId()].num_staged_ios_ > 0);
}

// pgaio_submit_staged(void)
void StorageManager::submitStagedIO() {
    IOContext& my_io_ctx = io_ctx_pool_[myContextId()];

    if (my_io_ctx.num_staged_ios_ == 0) {
        return;
    }
    // submit staged IO to io uring manager
    io_uring_mgr_.submit(my_io_ctx.num_staged_ios_, my_io_ctx.staged_ios_);
    my_io_ctx.num_staged_ios_ = 0;
}

// FileWriteV(File file, const struct iovec *iov, int iovcnt, off_t offset,
void StorageManager::writev(int fd, uint32_t block_num, void** buffers, uint32_t num_blocks, bool skip_fsync) {
    int64_t seek_pos = PAGE_SIZE * block_num;
    iovec iov[MAX_IOV];
    int	iovcnt = buffersToIovecs(iov, buffers, num_blocks);
    size_t bytes_to_write = PAGE_SIZE * num_blocks;

    while(bytes_to_write > 0) {
        int nbytes = pwritev(fd, iov, iovcnt, seek_pos);
        if (nbytes < 0) {
			std::cerr << "Error writing to file " << fd << "\n";
		}
        bytes_to_write -= nbytes;
        seek_pos += nbytes;
        // TODO implement compute_remaining_iovec for > 1 number of blocks 
        iov[0].iov_base = reinterpret_cast<char*>(iov[0].iov_base) + nbytes;
        iov[0].iov_len -= nbytes; 
    }
    // TODO fsync if needed
}

StorageManager& StorageManager::getInstanceImpl() {
    static StorageManager instance;
    return instance;
}

int StorageManager::myContextId() {
    static std::atomic<int> counter = 0;
    // each thread gets a unique ctx id if not initialized
    static thread_local int my_ctx_id = counter.fetch_add(1);
    return my_ctx_id;
}

// buffers_to_iovec(struct iovec *iov, void **buffers, int nblocks)
int StorageManager::buffersToIovecs(iovec* iov, void** buffers, int num_blocks) {
    iovec* iovp = iov;
    iovp->iov_base = buffers[0];
    iovp->iov_len = PAGE_SIZE;
    int iovcnt = 1;

    for (int i = 1; i < num_blocks; i++) {
        if ((reinterpret_cast<char*>(iovp->iov_base) + iovp->iov_len) == buffers[i]) {
            iovp->iov_len += PAGE_SIZE;
        }
        else {
            iovp++;
            iovp->iov_base = buffers[i];
            iovp->iov_len = PAGE_SIZE;
            iovcnt++;
        }
    }

    return iovcnt;
}

StorageManager::StorageManager() : io_uring_mgr_(IOURingManager::getInstance()) {
    num_workers_ = std::stoi(Config::getInstance().get("gc.num_workers"));
    io_queue_depth_ = std::stoi(Config::getInstance().get("gc.io_uring.queuedepth"));
    // allocate all the struct pools    
    io_ctx_pool_.resize(num_workers_);
    ioh_pool_.resize(num_workers_ * io_queue_depth_);
    iovec_pool_.resize(num_workers_ * io_queue_depth_ * MAX_IOV);
    // intialize the structs
    for (int i = 0; i < num_workers_; i++) {
        io_ctx_pool_[i].ioh_idx_ = i * io_queue_depth_;
        for (int j = 0; j < io_queue_depth_; j++) {
            int idx = i * io_queue_depth_ + j;
            io_ctx_pool_[i].free_ioh_.pushBack(&ioh_pool_[idx].node_);
            ioh_pool_[idx].ctx_id_ = i;
            ioh_pool_[idx].iovec_off_ = idx * MAX_IOV;
        }
    }
}

// precondition: the ctx ID of the handle & the thread are the same 
void StorageManager::reclaimIOHandle(IOHandle* ioh) {
    IOContext& my_io_ctx = io_ctx_pool_[myContextId()];

    if (ioh->state_ != IOHandleState::HANDED_OUT) {
        my_io_ctx.in_flight_ioh_.remove(&ioh->node_);
    }
    ioh->generation_++;
    ioh->updateState(IOHandleState::IDLE);
    std::atomic_thread_fence(std::memory_order_release);
    
    // TODO reset other fields 

    my_io_ctx.free_ioh_.pushFront(&ioh->node_);
}

// pgaio_io_wait_for_free(void)
void StorageManager::waitForFreeIOHandle() {
    int	reclaimed = 0;
    IOContext& my_io_ctx = io_ctx_pool_[myContextId()];

    for (int i = 0; i < io_queue_depth_; i++) {
        IOHandle* ioh = &ioh_pool_[my_io_ctx.ioh_idx_ + i];
        if (ioh->state_ == IOHandleState::COMPLETED_SHARED) {
            std::atomic_thread_fence(std::memory_order_acquire);
            reclaimIOHandle(ioh);
            reclaimed++;
        }
    }

    if (reclaimed > 0) {
        return;
    }

    // should never happen as we currently do not support batch mode
    if (my_io_ctx.num_staged_ios_ > 0) {
        submitStagedIO();
    }

    // somehow handles could have been freed during submission... how?
    if (!my_io_ctx.free_ioh_.isEmpty()) {
        return;
    }

    // Wait for the oldest inflight IO
    DListNode* head = my_io_ctx.in_flight_ioh_.front();
    // Get the parent IOHandle of the head node
    IOHandle* ioh = reinterpret_cast<IOHandle*>(reinterpret_cast<char*>(head) - (offsetof(IOHandle, node_)));
    uint64_t generation = ioh->generation_;
    switch(ioh->state_) {
        case IOHandleState::IDLE:
		case IOHandleState::DEFINED:
		case IOHandleState::HANDED_OUT:
        case IOHandleState::STAGED:
        case IOHandleState::COMPLETED_LOCAL:
            std::cerr << "IOHandle in incorrect state\n";
            break;
        case IOHandleState::COMPLETED_IO:
        case IOHandleState::SUBMITTED:
            waitForIO(ioh, generation);
        case IOHandleState::COMPLETED_SHARED:
            // another backend might have finished this IO
            std::atomic_thread_fence(std::memory_order_acquire);
            reclaimIOHandle(ioh);
            break;
    }
        
    return;

}

// pgaio_io_wait(PgAioHandle *ioh, uint64 ref_generation)
void StorageManager::waitForIO(IOHandle* ioh, uint64_t ref_gen) {
    IOHandleState state;
    bool am_owner = (ioh->ctx_id_ == myContextId());
    if (ioh->recycled(ref_gen, &state)) {
        return;
    }
    
    while (true) {
        if (ioh->recycled(ref_gen, &state)) {
            return;
        }

        switch(state) {
            case IOHandleState::IDLE:
            case IOHandleState::HANDED_OUT:
                std::cerr << "IOHandle in incorrect state\n";
                break;
            case IOHandleState::SUBMITTED:
                io_uring_mgr_.waitOne(ioh, ref_gen);
                continue;
            case IOHandleState::DEFINED:
            case IOHandleState::STAGED:
            case IOHandleState::COMPLETED_IO: {
                    std::unique_lock<std::mutex> lock(ioh->mtx_);
                    ioh->cv_.wait(lock, 
                            [ioh, &ref_gen, &state ]{ return (ioh->recycled(ref_gen, &state) 
                                || state == IOHandleState::COMPLETED_LOCAL 
                                || state == IOHandleState::COMPLETED_SHARED); });
                    lock.unlock();
                }
				break;
            case IOHandleState::COMPLETED_SHARED:
            case IOHandleState::COMPLETED_LOCAL:
                if (am_owner) {
                    reclaimIOHandle(ioh);
                }
                return;
        }
    }

}

// pgaio_io_stage(PgAioHandle *ioh, PgAioOp op)
void StorageManager::stageIO(IOHandle* ioh, IOOp ioop) {
    IOContext& my_io_ctx = io_ctx_pool_[myContextId()];
    
    // define the IO operation type
    ioh->op_ = ioop;
    ioh->updateState(IOHandleState::DEFINED);
    
    BufferManager::bufferStageCommon(ioh);
    ioh->updateState(IOHandleState::STAGED);
    //submit the staged IOs
    my_io_ctx.staged_ios_[my_io_ctx.num_staged_ios_++] = ioh;
    ioh->updateState(IOHandleState::SUBMITTED);
    my_io_ctx.in_flight_ioh_.pushBack(&ioh->node_);
    submitStagedIO();
}



}
#include <stdlib.h>

#include "boost/functional/hash_fwd.hpp"
#include "buffer/buffermanager.h"

namespace ercat {

void BufferManager::init() {
    getInstanceImpl();
}

BufferManager& BufferManager::getInstance() {
    return getInstanceImpl();    
}

// quick check without peeking the IO system
bool BufferManager::checkReadBuffersOperation(const ReadBuffersOperation* read_buf_op) {
    return (read_buf_op->nblocks_ == read_buf_op->nblocks_done_);
}

size_t BufferManager::hashBufTag(int fd, BlockNumber block_num) {
    std::size_t seed = 0;
    boost::hash_combine(seed, fd);
    boost::hash_combine(seed, block_num);
    return seed;
}
    
size_t BufferManager::mapPartition(size_t hash_val) {
    return (hash_val % PARTITION_NUM);
}

// TODO
BufferManager::~BufferManager() {
    free(buffer_blocks_);
}

// initialize the io_wref of buffer, using ioh
void BufferManager::bufferStageCommon(IOHandle* ioh) {
    IOWaitRef io_wref;
    io_wref.ioh_ = ioh;
    io_wref.generation_ = ioh->generation_;
    
    Buffer buffer = ioh->buffer_;
    BufferDesc* buf_desc = &getInstance().buffer_descs_[buffer];
    
    uint32_t buf_state = buf_desc->lock();
    
    buf_state += BUF_REFCOUNT_ONE;
	buf_desc->io_wref_ = io_wref;
    
    buf_desc->unlock(buf_state);
}

// buffer_readv_complete_one(PgAioTargetData *td, uint8 buf_off, Buffer buffer,
// TerminateBufferIO(BufferDesc *buf, bool clear_dirty, uint32 set_flag_bits,
void BufferManager::bufferReadvCompleteOne(IOHandle* ioh) {
    Buffer buffer = ioh->buffer_;
    BufferDesc* buf_desc = &getInstance().buffer_descs_[buffer];
    uint32_t buf_state = buf_desc->lock();
    buf_state &= ~BM_IO_IN_PROGRESS;
    // buffer no longer pinned by IO system.
    buf_state -= BUF_REFCOUNT_ONE;
    buf_desc->io_wref_.reset();
    buf_state |= BM_VALID;
    buf_desc->unlock(buf_state);
}

Buffer BufferManager::readBuffer(int fd, BlockNumber block_num, BufferStrategy buf_strat) {
    ReadBuffersOperation read_buf_op;
    read_buf_op.fd_ = fd;
    read_buf_op.block_num_ = block_num;
    read_buf_op.nblocks_ = 1;
    read_buf_op.nblocks_done_ = 0;
    read_buf_op.buf_strat_ = buf_strat;

    if (startReadBuffer(&read_buf_op)) {
        waitReadBuffers(&read_buf_op);
        return read_buf_op.buffers_[0];
    }

    return -1;
}

// StartReadBuffersImpl(ReadBuffersOperation *operation,
bool BufferManager::startReadBuffer(ReadBuffersOperation* read_buf_op) {
    bool found;
    BufferDesc* buf_desc = bufferAlloc(read_buf_op, &found);
    read_buf_op->buffers_[0] = buf_desc->buffer_;

    // valid page is in buffer, no need for IO
    if (found) {
        read_buf_op->nblocks_done_ = 1;
        return false;
    }

    read_buf_op->wref_.reset();

    // otherwise start reading buffer
    asyncReadBuffers(read_buf_op);
    
    return true;
}

// WaitReadBuffers(ReadBuffersOperation *operation)
// Unlike PG, we do not check the error code of io_uring as ioh may no longer be valid
void BufferManager::waitReadBuffers(ReadBuffersOperation* read_buf_op) {
    if (read_buf_op->wref_.valid()) {
        str_mgr_.waitOnIOWaitRef(&read_buf_op->wref_);
        read_buf_op->nblocks_done_ = 1;
    }
}

// ReleaseBuffer(Buffer buffer)
void BufferManager::releaseBuffer(Buffer buffer) {
    BufferDesc* buf_desc = &buffer_descs_[buffer];
    unpinBuffer(buf_desc);
}

void BufferManager::markBufferDirty(Buffer buffer) {
    uint32_t buf_state, old_buf_state;
    BufferDesc* buf_desc = &buffer_descs_[buffer];

    while (true) {
        if (old_buf_state & BM_LOCKED) {
            old_buf_state = buf_desc->waitUnlocked();
        }
        else {
            old_buf_state = buf_desc->state_.load();
        }

        buf_state = old_buf_state;
        buf_state |= BM_DIRTY | BM_JUST_DIRTIED;

        if (buf_desc->state_.compare_exchange_strong(old_buf_state, buf_state)) {
            break;
        }
    }
}

// BufferGetBlock(Buffer buffer)
void* BufferManager::getBufferBlock(Buffer buffer) {
    return &reinterpret_cast<char*>(buffer_blocks_)[PAGE_SIZE * buffer];
}

BufferManager& BufferManager::getInstanceImpl() {
    static BufferManager instance;
    return instance;
}

// TODO may consider local counters to prevent pinning twice 
// PinBuffer(BufferDesc *buf, BufferAccessStrategy strategy,
bool BufferManager::pinBuffer(BufferDesc* buf_desc) {
    uint32_t buf_state, old_buf_state;
    bool result;
    
    while (true) {
        if (old_buf_state & BM_LOCKED) {
            old_buf_state = buf_desc->waitUnlocked();
        }
        else {
            old_buf_state = buf_desc->state_.load();
        }

        buf_state = old_buf_state;
        buf_state += BUF_REFCOUNT_ONE;
        // increment usage count if not maximum
        if (BUF_STATE_GET_USAGECOUNT(buf_state) < BM_MAX_USAGE_COUNT) {
            buf_state += BUF_USAGECOUNT_ONE;
        }

        if (buf_desc->state_.compare_exchange_strong(old_buf_state, buf_state)) {
            result = (buf_state & BM_VALID) != 0;
            break;
        }
    }

    return result;
}

// UnpinBufferNoOwner(BufferDesc *buf)
void BufferManager::unpinBuffer(BufferDesc* buf_desc) {
    uint32_t buf_state, old_buf_state;

    while (true) {
        if (old_buf_state & BM_LOCKED) {
            old_buf_state = buf_desc->waitUnlocked();
        }
        else {
            old_buf_state = buf_desc->state_.load();
        }

        buf_state = old_buf_state;
        buf_state -= BUF_REFCOUNT_ONE;

        if (buf_desc->state_.compare_exchange_strong(old_buf_state, buf_state)) {
            break;
        }
    }
}

BufferManager::BufferManager() : str_mgr_(StorageManager::getInstance()) {
    int num_blocks = std::stoi(Config::getInstance().get("gc.buffer.numblocks"));
    buffer_table_.init(num_blocks);
    buffer_partition_locks_.resize(PARTITION_NUM);
    buffer_descs_.resize(num_blocks);
    buffer_blocks_ = aligned_alloc(PAGE_SIZE, num_blocks * PAGE_SIZE);
    // Buffer Descriptor points to block in order
    for (int i = 0; i < num_blocks; i++) {
        buffer_descs_[i].buffer_ = i;
    }
    //TODO initialize policies
}

// BufferAlloc(SMgrRelation smgr, char relpersistence, ForkNumber forkNum,
BufferDesc* BufferManager::bufferAlloc(ReadBuffersOperation* read_buf_op, bool* found) {
    int fd = read_buf_op->fd_;
    int block_num = read_buf_op->block_num_;

    size_t hash_val = hashBufTag(fd, block_num);
    PaddedSharedMutex& partition_mtx = mapPartitionLock(hash_val);

    std::shared_lock<std::shared_mutex> slock(partition_mtx.mtx_);
    int buf_id = buffer_table_.get(fd, block_num, hash_val, found);
    if (*found) {
        BufferDesc* buf_desc = &buffer_descs_[buf_id];
        bool valid = pinBuffer(buf_desc);
        slock.unlock();
        // if the page is being read, the page is in place, but not valid
        *found &= valid;

        return buf_desc;
    }
    slock.unlock();

    Buffer victim_buffer = getVictimBuffer(read_buf_op->buf_strat_);
    BufferDesc* victim_buf_desc = &buffer_descs_[victim_buffer];

    // exclusive lock on the partition
    std::unique_lock<std::shared_mutex> xlock(partition_mtx.mtx_);
    buf_id = buffer_table_.insert(fd, block_num, hash_val, victim_buffer);

    // someone already started reading in the page
    if (buf_id >= 0) {
        unpinBuffer(victim_buf_desc);
        BufferDesc* buf_desc = &buffer_descs_[buf_id];
        bool valid = pinBuffer(buf_desc);
        xlock.unlock();
        // if the page is being read, the page is in place, but not valid
        *found &= valid;

        return buf_desc;
    }

    uint32_t victim_buf_state = victim_buf_desc->lock();
    victim_buf_desc->fd_ = read_buf_op->fd_;
    victim_buf_desc->block_num_ = read_buf_op->block_num_;
    victim_buf_state |= BM_TAG_VALID | BUF_USAGECOUNT_ONE;
    victim_buf_desc->unlock(victim_buf_state);

    xlock.unlock();

    *found = false;

    return victim_buf_desc;
}

// GetVictimBuffer(BufferAccessStrategy strategy, IOContext io_context)
Buffer BufferManager::getVictimBuffer(BufferStrategy buf_strat) {
    Buffer buf = -1;
    uint32_t buf_state;
    
    while (true) {
        // TODO buf_desc is returned in pinned state.
        BufferDesc* buf_desc = strategyGetBuffer(buf_strat, &buf_state);
        buf = buf_desc->buffer_;
        if (buf_state & BM_DIRTY) {
            std::shared_lock<std::shared_mutex> content_lock(buf_desc->content_lock_, std::defer_lock);
            content_lock.try_lock();
            // To prevent deadlock, preemtively give up if someone already exclusively locked it. 
            if (!content_lock.owns_lock()) {
                unpinBuffer(buf_desc);
                continue;
            }

            /* OK, do the I/O */
            flushBuffer(buf_desc);
		    content_lock.unlock();
            //ScheduleBufferTagForWriteback(&BackendWritebackContext, io_context,
			//						  &buf_hdr->tag);            

        }
        // someone else could pin the buffer in between, in which case, give up on this buffer. 
        if ((buf_state & BM_TAG_VALID) && !invalidateVictimBuffer(buf_desc)) {
            unpinBuffer(buf_desc);
            continue;
        }

        break;
    }

    return buf;
}

// AsyncReadBuffers(ReadBuffersOperation *operation, int *nblocks_progress)
bool BufferManager::asyncReadBuffers(ReadBuffersOperation* read_buf_op) {
    IOHandle* ioh = str_mgr_.acquireIOHandleNB();
    Buffer buffer = read_buf_op->buffers_[0];
    void * io_pages[32];
    bool did_start_io;

    if (ioh == nullptr) {
        str_mgr_.submitStagedIO();
        ioh = str_mgr_.acquireIOHandle();
    }

    if (!readBuffersCanStartIO(buffer, false)) {
        read_buf_op->nblocks_done_ = 1;
        str_mgr_.releaseIOHandle(ioh);
        read_buf_op->wref_.reset();
        did_start_io = false;
    }
    else {
        io_pages[0] = getBufferBlock(buffer);
        /* get a reference to wait for in WaitReadBuffers() */
        read_buf_op->wref_.ioh_ = ioh;
        read_buf_op->wref_.generation_ = ioh->generation_;

		/* provide the list of buffers to the completion callbacks */
        ioh->buffer_ = buffer;

        str_mgr_.startReadV(ioh, read_buf_op->fd_, read_buf_op->block_num_, io_pages, read_buf_op->nblocks_);
        did_start_io = true;
    }


    return did_start_io;
}

// ReadBuffersCanStartIO(Buffer buffer, bool nowait)
bool BufferManager::readBuffersCanStartIO(Buffer buffer, bool no_wait) {
    // submit any pending IO now if there is no plan to submit IO for the current buffer later.
    if (!no_wait && str_mgr_.haveStagedIO()) {
        if (startBufferIO(&buffer_descs_[buffer], true, true)) {
            return true;
        }
        // buffer is likely already valid, so submit now, since there is no plan for submission later.
        str_mgr_.submitStagedIO();
    }

    return startBufferIO(&buffer_descs_[buffer], true, no_wait);
}

// TODO: fix this back so we prevent redundant flushes
// StartBufferIO(BufferDesc *buf, bool forInput, bool nowait)
bool BufferManager::startBufferIO(BufferDesc* buf_desc, bool read, bool no_wait) {
    uint32_t buf_state;
    if (read) {
        while (true) {
            buf_state = buf_desc->lock();
            if (!(buf_state & BM_IO_IN_PROGRESS)) {
                break;
            }
            buf_desc->unlock(buf_state);
            if (no_wait) {
                return false;
            }
            // IO is submitted by another backend
            waitIO(buf_desc);
        }
    }
    else {
        buf_state = buf_desc->lock();
    }
    
    if (read ? (buf_state & BM_VALID) : !(buf_state & BM_DIRTY))
	{
		buf_desc->unlock(buf_state);
        return false;
	}

	buf_state |= BM_IO_IN_PROGRESS;
    buf_desc->unlock(buf_state);

	return true;
}

// WaitIO(BufferDesc *buf)
// taking out the condition variable logic in the original PG code for read operation, due to redundancy
// there may be extra spin loops as a result
void BufferManager::waitIO(BufferDesc* buf_desc) {
    while (true) {
        uint32_t buf_state = buf_desc->lock();
        IOWaitRef io_wref = buf_desc->io_wref_;
        buf_desc->unlock(buf_state);

        if (!(buf_state & BM_IO_IN_PROGRESS)) {
            break;
        }
        // this can cause spin loops
        if (io_wref.valid()) {
			str_mgr_.waitOnIOWaitRef(&io_wref);
            continue;
        }
    }
}

PaddedSharedMutex& BufferManager::mapPartitionLock(size_t hash_val) {
    return buffer_partition_locks_[hash_val % PARTITION_NUM];
}


// InvalidateVictimBuffer(BufferDesc *buf_hdr)
bool BufferManager::invalidateVictimBuffer(BufferDesc* buf_desc) {
    // apparently safe because the descriptor is pinned...
    int fd = buf_desc->fd_;
    BlockNumber block_num = buf_desc->block_num_;
    size_t hash_val = hashBufTag(fd, block_num);
    // take the partition lock
    PaddedSharedMutex& partition_mtx = mapPartitionLock(hash_val);
    std::unique_lock<std::shared_mutex> xlock(partition_mtx.mtx_);
    
    // lock the buf desc
    uint32_t buf_state = buf_desc->lock();
    // buffer is pinned by another thread or is dirtied
    if (BUF_STATE_GET_REFCOUNT(buf_state) != 1 || (buf_state & BM_DIRTY)) {
        buf_desc->unlock(buf_state);
		xlock.unlock();

		return false;
    }

    buf_desc->fd_ = -1;
    buf_desc->block_num_ = 0;
    // clear out all the flags, including valid bits etc
    buf_state &= ~(BUF_FLAG_MASK | BUF_USAGECOUNT_MASK);
    // delete the entry in the buffer table
    buffer_table_.remove(fd, block_num, hash_val);
    buf_desc->unlock(buf_state);

    xlock.unlock();

    return true;
}

// Does not handle double flushing etc, since we do not support logging
// TODO: fix double flushing issue
// FlushBuffer(BufferDesc *buf, SMgrRelation reln, IOObject io_object,
// TerminateBufferIO(BufferDesc *buf, bool clear_dirty, uint32 set_flag_bits,
void BufferManager::flushBuffer(BufferDesc* buf_desc) {
    if (!startBufferIO(buf_desc, false, false)) {
        return;
    }

    uint32_t buf_state = buf_desc->lock();
    /* To check if block content changes while flushing. */
    buf_state &= ~BM_JUST_DIRTIED;
    buf_desc->unlock(buf_state);

    void* buf_to_write = getBufferBlock(buf_desc->block_num_);
    str_mgr_.writev(buf_desc->fd_, buf_desc->block_num_, &buf_to_write, 1, false);

    buf_state = buf_desc->lock();
    buf_state &= ~BM_IO_IN_PROGRESS;
    // if not just dirtied, clear dirty bit
    if (!(buf_state & BM_JUST_DIRTIED)) {
        buf_state &= ~(BM_DIRTY | BM_CHECKPOINT_NEEDED);
    }
    buf_desc->unlock(buf_state);
}

}
#pragma once
#include <cstdint>
#include <shared_mutex>

#include "storage/storage.h"
#include "utils/spinlock.h"

namespace ercat {

constexpr uint32_t BUF_REFCOUNT_BITS = 18;
constexpr uint32_t BUF_USAGECOUNT_BITS = 4;
constexpr uint32_t BUF_FLAG_BITS = 10;

constexpr uint32_t BUF_REFCOUNT_ONE = 1;
constexpr uint32_t BUF_REFCOUNT_MASK = ((1U << BUF_REFCOUNT_BITS) - 1);
constexpr uint32_t BUF_USAGECOUNT_MASK = (((1U << BUF_USAGECOUNT_BITS) - 1) << (BUF_REFCOUNT_BITS));
constexpr uint32_t BUF_USAGECOUNT_ONE = (1U << BUF_REFCOUNT_BITS);
constexpr uint32_t BUF_USAGECOUNT_SHIFT = BUF_REFCOUNT_BITS;
constexpr uint32_t BUF_FLAG_MASK = (((1U << BUF_FLAG_BITS) - 1) << (BUF_REFCOUNT_BITS + BUF_USAGECOUNT_BITS));

constexpr uint32_t BUF_STATE_GET_REFCOUNT(uint32_t state) {
    return (state) & BUF_REFCOUNT_MASK;
}

constexpr uint32_t BUF_STATE_GET_USAGECOUNT(uint32_t state) {
    return ((state) & BUF_USAGECOUNT_MASK) >> BUF_USAGECOUNT_SHIFT;
}

constexpr uint32_t BM_LOCKED = (1U << 22);
constexpr uint32_t BM_DIRTY = (1U << 23);
constexpr uint32_t BM_VALID = (1U << 24);
constexpr uint32_t BM_TAG_VALID = (1U << 25);
constexpr uint32_t BM_IO_IN_PROGRESS = (1U << 26);
constexpr uint32_t BM_IO_ERROR = (1U << 27);
constexpr uint32_t BM_JUST_DIRTIED = (1U << 28);
constexpr uint32_t BM_PIN_COUNT_WAITER = (1U << 29);
constexpr uint32_t BM_CHECKPOINT_NEEDED = (1U << 30);
constexpr uint32_t BM_PERMANENT = (1U << 31);

constexpr uint32_t BM_MAX_USAGE_COUNT = 5;

constexpr unsigned int PARTITION_NUM = 128;

typedef uint32_t Buffer;

typedef uint32_t BlockNumber;

struct BufferStrategy {
    BufferStrategy() : policy_no_(0), use_ring_(false) { }
    void reset() {
        policy_no_ = 0;
        use_ring_ = false;
    }

    // policy partition number
    int16_t policy_no_;
    // whether to use ring buffer
    bool use_ring_;
};

struct BufferDesc {
    BufferDesc() : fd_(-1), block_num_(0), buffer_(0), io_wref_(), state_(0) { }
    uint32_t lock() {
        uint32_t old_buf_state;
        SpinDelay spin_delay;
        while (true) {
            old_buf_state = state_.fetch_or(BM_LOCKED);
            if (!(old_buf_state & BM_LOCKED)) {
                break;
            }
            spin_delay.spinDelay();
        }
        return old_buf_state | BM_LOCKED;
    }

    void unlock(uint32_t state) {
        std::atomic_thread_fence(std::memory_order_release);
        state_.store(state & (~BM_LOCKED));
    }

    uint32_t waitUnlocked() {
        uint32_t buf_state;
        SpinDelay spin_delay;
        buf_state = state_.load();
        while (buf_state & BM_LOCKED) {
            spin_delay.spinDelay();
            buf_state = state_.load();
        }
        return buf_state;
    }

    int fd_;
    BlockNumber block_num_;
    Buffer buffer_;
    IOWaitRef io_wref_;
    std::atomic<uint32_t> state_;
    std::shared_mutex content_lock_;
};

struct ReadBuffersOperation {
    ReadBuffersOperation() : nblocks_(0), nblocks_done_(0) { }
    void reset() {
        nblocks_ = 0;
        nblocks_done_ = 0;
        buf_strat_.reset();
        wref_.reset();
    }

    // file descriptor
    int32_t fd_;
    // Buffer Ids to read to
    Buffer buffers_[4];
    // Starting BlockNumber
    BlockNumber block_num_;
    // number of blocks requested
    int16_t nblocks_;
    // number of blocks with completed IO
    int16_t nblocks_done_;
    // buffer strategy
    BufferStrategy buf_strat_;
    // IO handle to wait on
    IOWaitRef wref_;
};

}



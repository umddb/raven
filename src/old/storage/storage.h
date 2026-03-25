#pragma once

#include <condition_variable>
#include <mutex>

namespace ercat {

const unsigned int PAGE_SIZE = 8192;

const unsigned int MAX_IOV = 128;

// IO types 
enum class IOOp : char {
    READV,
    WRITEV,
    INVALID
};

// IO target
struct IOOpData {
    int	fd_;
	uint16_t iov_length_;
	uint64_t offset_;
};

enum class IOHandleState : char {
	/* not in use */
    IDLE,

	/*
	 * Returned by pgaio_io_acquire(). The next state is either DEFINED (if
	 * pgaio_io_start_*() is called), or IDLE (if pgaio_io_release() is
	 * called).
	 */
	HANDED_OUT,

	/*
	 * pgaio_io_start_*() has been called, but IO is not yet staged. At this
	 * point the handle has all the information for the IO to be executed.
	 */
	DEFINED,

	/*
	 * stage() callbacks have been called, handle ready to be submitted for
	 * execution. Unless in batchmode (see c.f. pgaio_enter_batchmode()), the
	 * IO will be submitted immediately after.
	 */
	STAGED,

	/* IO has been submitted to the IO method for execution */
	SUBMITTED,

	/* IO finished, but result has not yet been processed */
	COMPLETED_IO,

	/*
	 * IO completed, shared completion has been called.
	 *
	 * If the IO completion occurs in the issuing backend, local callbacks
	 * will immediately be called. Otherwise the handle stays in
	 * COMPLETED_SHARED until the issuing backend waits for the completion of
	 * the IO.
	 */
	COMPLETED_SHARED,

	/*
	 * IO completed, local completion has been called.
	 *
	 * After this the handle will be made reusable and go into IDLE state.
	 */
	COMPLETED_LOCAL
};

struct IOHandle {
    IOHandle()  : state_(IOHandleState::IDLE), op_(IOOp::INVALID), generation_(0) { }
    ~IOHandle() { }
    void updateState(IOHandleState new_state) {
        std::atomic_thread_fence(std::memory_order_release);
        state_ = new_state;
    }
    bool recycled(uint64_t ref_gen, IOHandleState* state) {
        *state = state_;
        std::atomic_thread_fence(std::memory_order_acquire);
        return (ref_gen != generation_);
    }

    // thread context id
    int ctx_id_;
    // current state of the IO
    IOHandleState state_;
    // IO operation type
    IOOp op_;
    // Offset of the corresponding iovec struct
    uint32_t iovec_off_;
    // IO target
    IOOpData op_data_;
    // current generation of the handle for recycling
    uint64_t generation_;
    // embedded list structure for efficient allocation / deallocation
    DListNode node_;
    // buffer the IO is associated with
    uint32_t buffer_;
    // result of the IO operation
    int32_t result_;
    // mutex for waiting on IO, originally a spin lock in Postgres
    std::mutex mtx_;
    // condition variable for waiting on IO
    std::condition_variable cv_;
};

struct IOWaitRef {
    IOWaitRef() : ioh_(nullptr), generation_(0) { }
    void reset() {
        ioh_ = nullptr;
        generation_ = 0;
    }
    bool valid() {
        return (ioh_ != nullptr);
    }

	IOHandle* ioh_;
	uint64_t generation_;
};

// per thread I/O context
struct alignas(64) IOContext {
    // properly initialized in the storage manager
    IOContext() : ioh_idx_(0), num_staged_ios_(0) { }
    ~IOContext() { }

    // index into IOHandle partition
    uint32_t ioh_idx_;
    // List of free/available IOHandle for use
    DList free_ioh_;
    // Number of current staged IOs
    uint16_t num_staged_ios_;
    // Array holding IOHandle for staged IOs
    IOHandle* staged_ios_[32];
    // List of IOHandles for in flight IO
    DList in_flight_ioh_;
};

}
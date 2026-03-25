#include <algorithm>
#include <atomic>
#include <iostream>
#include <mutex>

#include "storage/iouringmanager.h"
#include "storage/storagemanager.h"
#include "utils/config.h"

namespace ercat {
    
void IOURingManager::init() {
    getInstanceImpl();
}

IOURingManager& IOURingManager::getInstance() {
    return getInstanceImpl();
}

IOURingManager::~IOURingManager() {
    for (auto& ctx : io_uring_ctx_) {
        std::lock_guard<std::shared_mutex> lock(ctx.mtx_);
        io_uring_queue_exit(&ctx.io_uring_ring_);
    }
}

// precondition: the number of staged_ios is not big enough to 
// fill up the sq.   
int IOURingManager::submit(int num_ios, IOHandle** staged_ios) {
    io_uring* uring = &io_uring_ctx_[myContextId()].io_uring_ring_;
    for (int i = 0; i < num_ios; i++) {
        IOHandle* ioh = staged_ios[i];
        io_uring_sqe* sqe = io_uring_get_sqe(uring);
        IoToSqe(ioh, sqe);
        while (true) {
            int ret = io_uring_submit(uring);
            if (ret == num_ios) {
                break;
            }
            else {
                std::cerr << "io_uring submission failed with return value " << ret << "\n";
            }
        }
        
    }

    return num_ios;
    
}

void IOURingManager::waitOne(IOHandle* ioh, uint64_t ref_gen) {
	IOHandleState state;
	IOURingContext* owner_context = &io_uring_ctx_[ioh->ctx_id_];
	bool expect_cqe;
	int waited = 0;

    std::unique_lock<std::shared_mutex> cq_lock(owner_context->mtx_);

	while (true) {
		if (ioh->recycled(ref_gen, &state) || state != IOHandleState::SUBMITTED) {
			/* the IO was completed by another backend */
			break;
		}
		else if (io_uring_cq_ready(&owner_context->io_uring_ring_)) {
			/* no need to wait in the kernel, io_uring has a completion */
			expect_cqe = true;
		}
		else {
			int ret;
			io_uring_cqe* cqes;
			ret = io_uring_wait_cqes(&owner_context->io_uring_ring_, &cqes, 1, NULL, NULL);

			if (ret == -EINTR) {
				continue;
			}
			else if (ret != 0) {
				/* see comment after io_uring_submit() */
				errno = -ret;
				std::cerr << "io_uring wait failed:" << errno << "\n";
			}
			else {
				expect_cqe = true;
				waited++;
			}
		}

		if (expect_cqe) {
			drainCQ(owner_context);
		}
	}

	cq_lock.unlock();
}

IOURingManager& IOURingManager::getInstanceImpl() {
    static IOURingManager instance;
    return instance;
}

int IOURingManager::myContextId() {
    static std::atomic<int> counter = 0;
    // each thread gets a unique ctx id if not initialized
    static thread_local int my_ctx_id = counter.fetch_add(1);
    return my_ctx_id;
}

void IOURingManager::IoToSqe(IOHandle* ioh, io_uring_sqe* sqe) {
    iovec *iov;
    StorageManager& str_mgr = StorageManager::getInstance();
    switch (ioh->op_) {
        case IOOp::READV:
            iov = &str_mgr.iovecs()[ioh->iovec_off_];
			if (ioh->op_data_.iov_length_ == 1) {
				io_uring_prep_read(sqe,
								   ioh->op_data_.fd_,
								   iov->iov_base,
								   iov->iov_len,
								   ioh->op_data_.offset_);
			}
			else {
				io_uring_prep_readv(sqe,
									ioh->op_data_.fd_,
									iov,
									ioh->op_data_.iov_length_,
									ioh->op_data_.offset_);
			}
			break;
        case IOOp::WRITEV:
			iov = &str_mgr.iovecs()[ioh->iovec_off_];
			if (ioh->op_data_.iov_length_ == 1) {
				io_uring_prep_write(sqe,
									ioh->op_data_.fd_,
									iov->iov_base,
									iov->iov_len,
									ioh->op_data_.offset_);
			}
			else {
				io_uring_prep_writev(sqe,
									 ioh->op_data_.fd_,
									 iov,
									 ioh->op_data_.iov_length_,
									 ioh->op_data_.offset_);
			}
			break;
        default:
            break;
    }
    // sets the data region of sqe to be ioh
    io_uring_sqe_set_data(sqe, ioh);
}

void IOURingManager::drainCQ(IOURingContext* ctx) {
	int ready = io_uring_cq_ready(&ctx->io_uring_ring_);

	while (ready > 0) {
		io_uring_cqe* cqes[32];
		unsigned int ncqes;
        // peek to see if any cq is ready
		ncqes = io_uring_peek_batch_cqe(&ctx->io_uring_ring_, cqes, std::min(32, ready));

		ready -= ncqes;
        // process completed IO
		for (int i = 0; i < ncqes; i++) {
			io_uring_cqe* cqe = cqes[i];
			IOHandle* ioh = reinterpret_cast<IOHandle*>(io_uring_cqe_get_data(cqe));
			io_uring_cqe_seen(&ctx->io_uring_ring_, cqe);
            StorageManager::processCompleteIO(ioh, cqe->res);
		}
	}
}

IOURingManager::IOURingManager() {
    uint32_t num_workers = std::stoi(Config::getInstance().get("gc.num_workers"));
    uint32_t queue_depth = std::stoul(Config::getInstance().get("gc.io_uring.queuedepth"));
    
    io_uring_ctx_.resize(num_workers);
    // allocate the io_urings, one per worker thread
    for (auto& ctx : io_uring_ctx_) {
        int ret = io_uring_queue_init(queue_depth, &ctx.io_uring_ring_, 0);
        if (ret < 0) {
            std::cerr << "Failed to initialize io_uring\n";
        }
    }
}

}
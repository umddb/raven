#pragma once

#include <algorithm>
#include <atomic>
#include <thread>
#include <chrono>

#ifdef __x86_64__
    #include <immintrin.h>
#endif


namespace ercat {  

// spin delay struct used for spin locks
struct SpinDelay {
    static constexpr int min_delay = 1000;
    static constexpr int max_delay = 1000000;
    static constexpr int min_spins_per_delay = 10;
    static constexpr int max_spins_per_delay = 1000;
    static constexpr int default_spins_per_delay = 100;
    static constexpr int spins_per_delay = default_spins_per_delay;
    
    SpinDelay() : spins_(0), cur_delay_(0)  { }
   
    ~SpinDelay() { }
    
    void spinDelay() {
        // pause
        #ifdef __x86_64__
            _mm_pause();
        #elif __aarch64__
            __yield();
        #endif  

        if ((++spins_) > spins_per_delay) {
            if (cur_delay_ == 0) {
                cur_delay_ = min_delay;
            }

            std::this_thread::sleep_for(std::chrono::microseconds(cur_delay_));
            // double the delay everytime
            cur_delay_ += (cur_delay_);
            // if spin delay is maximum, go back to min
            if (cur_delay_ > max_delay) {
                cur_delay_ = min_delay;
            }

            spins_ = 0;
        }
    }

    int spins_;
    int cur_delay_;
};

// simple ttas spin lock, based on the spin delay above
class alignas(64) SpinLock {
public:
    SpinLock() {
        locked_.store(false);
    }

    ~SpinLock() { }

    void lock() {
        while (true) {
            if (!locked_.exchange(true)) {
                return;
            }

            SpinDelay spin_delay;
            while (locked_.load()) {
                spin_delay.spinDelay();
            }
        }
    }

    void unlock() {
        locked_.store(false);
    }

private:
    std::atomic<bool> locked_;
    char padding_[64 - sizeof(std::atomic<bool>)];
};

}
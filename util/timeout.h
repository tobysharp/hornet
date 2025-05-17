#pragma once

#include <chrono>
#include <numeric>

namespace hornet::util {

class Timeout {
 public:
    Timeout(int64_t timeout_ms) :
     timeout_ms_(timeout_ms),
     initial_time_(std::chrono::high_resolution_clock::now()),
     expiry_time_(initial_time_ + std::chrono::milliseconds{timeout_ms})
    {
    }

    bool IsExpired() const {
        if (timeout_ms_ < 0) return false;
        if (timeout_ms_ == 0) return true;
        return std::chrono::high_resolution_clock::now() >= expiry_time_;
    }

    int RemainingMs() const {
        using namespace std::chrono_literals;
        if (timeout_ms_ < 0) return -1;
        if (timeout_ms_ == 0) return 0;
        int64_t ms = std::max(0ms, std::chrono::duration_cast<std::chrono::milliseconds>(
            expiry_time_ - std::chrono::high_resolution_clock::now())).count();
        return static_cast<int>(std::min(ms, int64_t{std::numeric_limits<int>::max()}));
    }

    void Reset() {
        initial_time_ = std::chrono::high_resolution_clock::now();
        expiry_time_ = initial_time_ + std::chrono::milliseconds{timeout_ms_};
    }

 private:
   int64_t timeout_ms_;
   std::chrono::high_resolution_clock::time_point initial_time_;
   std::chrono::high_resolution_clock::time_point expiry_time_;
};

}  // namespace hornet::util

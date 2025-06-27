// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
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
    constexpr Timeout(const Timeout&) = default;
    constexpr Timeout(Timeout&&) = default;

    static Timeout Immediate() {
        return 0;
    }

    static Timeout Infinite() {
        return -1;
    }

    constexpr bool IsInfinite() const {
        return timeout_ms_ < 0;
    }

    constexpr bool IsImmediate() const {
        return timeout_ms_ == 0;
    }

    bool IsExpired() const {
        if (IsInfinite()) return false;
        if (IsImmediate()) return true;
        return std::chrono::high_resolution_clock::now() >= expiry_time_;
    }

    std::chrono::milliseconds RemainingMs() const {
        using namespace std::chrono_literals;
        std::chrono::milliseconds clamp_lo{IsInfinite() ? std::numeric_limits<int64_t>::max() : 0ll};
        std::chrono::milliseconds clamp_hi{IsImmediate() ? 0ll : -1ll};
        return std::clamp(std::chrono::duration_cast<std::chrono::milliseconds>(
            expiry_time_ - std::chrono::high_resolution_clock::now()), clamp_lo, clamp_hi);
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

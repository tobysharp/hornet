#pragma once

#include <random>
#include <cstdint>

namespace hornet::util {

inline uint64_t Rand64() {
    static thread_local std::random_device rd;
    static thread_local std::mt19937_64 gen(rd());
    static thread_local std::uniform_int_distribution<uint64_t> dist;

    return dist(gen);
}

}  // namespace hornet::util

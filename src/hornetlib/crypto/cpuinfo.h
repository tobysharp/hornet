// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <cstdint>

namespace hornet::crypto {

// CPU feature flags for cryptographic acceleration
struct CPUFeatures {
  bool has_sha_ni = false;   // Intel SHA Extensions
};

// Detect CPU features at runtime
// This function is thread-safe and caches results
inline const CPUFeatures& GetCPUFeatures() {
  static CPUFeatures features = []() {
    CPUFeatures f;

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#if defined(__GNUC__) || defined(__clang__)
    // Use GCC/Clang built-in CPU feature detection
    f.has_sha_ni = __builtin_cpu_supports("sha");

#elif defined(_MSC_VER)
    // MSVC: use __cpuidex intrinsic
    int cpu_info[4];

    // Check for CPUID support of function 7
    __cpuid(cpu_info, 0);
    int max_func = cpu_info[0];

    if (max_func >= 7) {
      __cpuidex(cpu_info, 7, 0);
      f.has_sha_ni = (cpu_info[1] & (1 << 29)) != 0;  // EBX bit 29
    }
#endif  // defined(__GNUC__) || defined(__clang__)
#endif  // defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)

    return f;
  }();

  return features;
}

// Helper functions for common queries
inline bool HasSHAExtensions() { return GetCPUFeatures().has_sha_ni; }

}  // namespace hornet::crypto

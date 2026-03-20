#pragma once

#include <cstdint>
#include <initializer_list>

namespace hornet::consensus::rules::scripts {

enum class VerifyFlag { P2SH, Witness };

[[nodiscard]] inline constexpr uint64_t CombineFlags(std::initializer_list<VerifyFlag> flags) {
  uint64_t result = 0;
  for (VerifyFlag flag : flags) result |= 1ull << static_cast<int>(flag);
  return result;  
}

[[nodiscard]] inline constexpr bool IsFlag(uint64_t flags, VerifyFlag flag) {
  return (flags & (1ull << static_cast<int>(flag))) != 0;
}

}  // namespace hornet::consensus::rules::scripts

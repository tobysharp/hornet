#pragma once

#include <array>
#include <span>

namespace hornet::util {

template <typename T, size_t N>
constexpr std::span<T> AsSpan(std::array<T, N>& arr) {
  return arr;
}

template <typename T, size_t N>
constexpr std::span<const T> AsSpan(const std::array<T, N>& arr) {
  return arr;
}

template <typename T>
inline std::span<const uint8_t> AsByteSpan(std::span<const T> input) {
  static_assert(std::is_trivially_copyable_v<T>, "AsByteSpan requires trivially copyable types");
  return {reinterpret_cast<const uint8_t*>(input.data()), input.size_bytes()};
}

template <typename T>
inline std::span<uint8_t> AsByteSpan(std::span<T> input) {
  static_assert(std::is_trivially_copyable_v<T>, "AsByteSpan requires trivially copyable types");
  return {reinterpret_cast<uint8_t*>(input.data()), input.size_bytes()};
}

}  // namespace hornet::util

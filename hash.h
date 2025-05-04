#include "sha256.h"
#include "types.h"

#include <span>

template <typename T> inline constexpr bool always_false_v = false;

// Iterator-based overload (useful for containers)
template <typename Iter> bytes32_t Sha256(Iter begin, Iter end) {
    using T = std::remove_reference_t<decltype(*begin)>;
    static_assert(std::is_trivially_copyable_v<T>, "Sha256: iterator value type must be trivially copyable.");
    const size_t count = static_cast<size_t>(end - begin);
    return SHA256::Hash(AsByteSpan<T>({count > 0 ? &*begin : nullptr, count}));
}

// Overload for trivially copyable types, spans, and ranges.
// Hashes trivially copyable objects or range-like containers by treating
// their contents as raw bytes. Not suitable for types with internal pointers or padding.
template <typename T>
inline bytes32_t Sha256(const T& value) {
  if constexpr (requires { value.begin(); value.end(); }) {
    return Sha256(value.begin(), value.end());
  } else if constexpr (std::is_trivially_copyable_v<T>) {
    return SHA256::Hash(AsByteSpan<T>({&value, 1}));
  } else {
    static_assert(always_false_v<T>, "Unsupported type passed to Sha256.");
  }
}

template <typename Iter> bytes32_t DoubleSha256(Iter begin, Iter end) {
    return Sha256(Sha256(begin, end));
}

// Overload for trivially copyable types, spans, and ranges
template <typename T>
inline bytes32_t DoubleSha256(const T& value) {
  return Sha256(Sha256(value));
}

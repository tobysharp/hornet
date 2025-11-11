#pragma once

#include <cstdint>
#include <tuple>

namespace hornet::data::utxo {

// Note that we intentionally store the offset in the high bits so that we can sort by offset
// simply by doing a numerical sort on the encoded uint64_t.

class IdCodec {
 public:
  struct Span {
    uint64_t offset;
    int length;
  };

  inline static Span Decode(uint64_t id) {
    return {id >> kLengthBits, static_cast<int>(id & kLengthMask) };
  }
  inline static uint64_t Encode(uint64_t offset, int length) {
    return (offset << kLengthBits) | (length & kLengthMask);
  }
  inline static uint64_t Offset(uint64_t id) {
    return Decode(id).offset;
  }
  inline static int Length(uint64_t id) {
    return Decode(id).length;
  }

 private:
  static constexpr int kLengthBits = 20;
  static constexpr int kLengthMask = (1 << kLengthBits) - 1;
  static constexpr int kOffsetBits = 64 - kLengthBits;
};

}  // namespace hornet::data::utxo

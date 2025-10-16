#pragma once

#include <cstdint.h>
#include <tuple>

namespace hornet::data::utxo {

class IdCodec {
 public:
  inline static std::pair<uint64_t, int> Decode(uint64_t id) {
    return {id >> kLengthBits, id & kLengthMask};
  }
  inline static uint64_t Encode(uint64_t offset, int length) {
    return (offset << kLengthBits) | (length & kLengthMask);
  }
  inline static int Length(uint64_t id) {
    return Decode(id).second;
  }
 private:
  static constexpr int kLengthBits = 20;
  static constexpr int kLengthMask = (1 << kLengthBits) - 1;
  static constexpr int kOffsetBits = 64 - kLengthBits;
};

}  // namespace hornet::data::utxo

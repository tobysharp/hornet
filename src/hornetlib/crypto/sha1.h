#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "hornetlib/crypto/sha.h"

namespace hornet::crypto::sha {

class SHA1Processor {
 public:
  using Hash = std::array<uint32_t, 5>;

  operator const Hash&() const { return H; }

  void operator()(const Block& M) {
    // Prepare the message schedule, W.
    Schedule W;
    for (int t = 0; t < 16; ++t) W[t] = M[t];
    for (int t = 16; t < 80; ++t) W[t] = ROTL<1>(W[t - 3] ^ W[t - 8] ^ W[t - 14] ^ W[t - 16]);

    uint32_t a = H[0];
    uint32_t b = H[1];
    uint32_t c = H[2];
    uint32_t d = H[3];
    uint32_t e = H[4];

    for (int t = 0; t < 80; ++t) {
      uint32_t T = ROTL<5>(a) + f(t, b, c, d) + e + K[t] + W[t];
      e = d;
      d = c;
      c = ROTL<30>(b);
      b = a;
      a = T;
    }

    H[0] += a;
    H[1] += b;
    H[2] += c;
    H[3] += d;
    H[4] += e;
  }

 private:
  using Schedule = std::array<uint32_t, 80>;

  static constexpr uint32_t f(int t, uint32_t x, uint32_t y, uint32_t z) {
    if (t < 20) return Ch(x, y, z);
    else if (t < 40 || t >= 60) return Parity(x, y, z);
    else return Maj(x, y, z);
  }

  static constexpr Schedule K = {
      0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999,
      0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999,
      0x5a827999, 0x5a827999, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1,
      0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1,
      0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc,
      0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc,
      0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6,
      0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6,
      0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6};

  Hash H = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
};

inline Bytes20 ComputeSHA1(std::span<const uint8_t> input) {
  SHA1Processor p;
  ComputeHash(p, input);
  return ReverseEndianWords<5>(p);
}

}  // namespace hornet::crypto::sha

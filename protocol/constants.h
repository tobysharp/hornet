#pragma once

#include <cstddef>
#include <cstdint>

namespace hornet::protocol {

// Magic numbers from https://en.bitcoin.it/wiki/Protocol_documentation
enum class Magic : uint32_t {
  Main = 0xD9B4BEF9,     // main
  Regtest = 0xDAB5BFFA,  // testnet/regnet
  Testnet = 0x0709110B,  // testnet3
  Signet = 0x40CF030A,   // signet
  Namecoin = 0xFEB4BEF9  // namecoin
};

inline constexpr size_t kCommandLength = 12;
inline constexpr size_t kHeaderLength = 24;
inline constexpr size_t kChecksumLength = 4;

// The maximum number of bytes accepted for an incoming message payload.
// See MAX_PROTOCOL_MESSAGE_LENGTH in https://github.com/bitcoin/bitcoin/blob/master/src/net.h.
inline constexpr size_t kMaxMessageSize = 4'000'000;

}  // namespace hornet::protocol

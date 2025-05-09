#pragma once

#include <cstdint>
#include <cstddef>

// Magic numbers from https://en.bitcoin.it/wiki/Protocol_documentation
enum class Magic : uint32_t {
    Main = 0xD9B4BEF9,      // main
    Regtest = 0xDAB5BFFA,   // testnet/regnet
    Testnet = 0x0709110B,   // testnet3
    Signet = 0x40CF030A,    // signet
    Namecoin = 0xFEB4BEF9   // namecoin
};

inline constexpr size_t kCommandLength = 12;
inline constexpr size_t kHeaderLength = 24;
#pragma once

#include <cstdint>

// Magic numbers from https://en.bitcoin.it/wiki/Protocol_documentation
enum class Magic : uint32_t {
    Main = 0xD9B4BEF9,      // main
    Testnet = 0xDAB5BFFA,   // testnet/regnet
    Testnet3 = 0x0709110B,  // signet(default)
    Namecoin = 0xFEB4BEF9   // namecoin
};

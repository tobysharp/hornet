#pragma once

#include "protocol/constants.h"

#include <cstdint>

constexpr const char* kLocalhost = "127.0.0.1";

constexpr uint16_t kMainnetPort = 8333;
constexpr uint16_t kRegtestPort = 18444;
constexpr uint16_t kSignetPort  = 38333;
constexpr uint16_t kTestnetPort = 18333;

enum class Network {
    Mainnet,
    Testnet,
    Regtest,
    Signet
};

inline constexpr Magic GetNetworkMagic(Network net) {
    switch (net) {
        case Network::Mainnet:
            return Magic::Main;
        case Network::Testnet:
            return Magic::Testnet;
        case Network::Regtest:
            return Magic::Regtest;
        case Network::Signet:
            return Magic::Signet;
    }
    __builtin_unreachable();
}

inline constexpr uint16_t GetNetworkPort(Network net) {
    switch (net) {
        case Network::Mainnet:
            return kMainnetPort;
        case Network::Testnet:
            return kTestnetPort;
        case Network::Regtest:
            return kRegtestPort;
        case Network::Signet:
            return kSignetPort;
    }
    __builtin_unreachable();
}
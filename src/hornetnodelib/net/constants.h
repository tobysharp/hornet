// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <cstdint>

#include "hornetlib/protocol/constants.h"

namespace hornet::node::net {

constexpr const char* kLocalhost = "127.0.0.1";

constexpr uint16_t kMainnetPort = 8333;
constexpr uint16_t kRegtestPort = 18444;
constexpr uint16_t kSignetPort = 38333;
constexpr uint16_t kTestnetPort = 18333;

enum class Network { Mainnet, Testnet, Regtest, Signet };

inline constexpr protocol::Magic GetNetworkMagic(Network net) {
  switch (net) {
    case Network::Mainnet:
      return protocol::Magic::Main;
    case Network::Testnet:
      return protocol::Magic::Testnet;
    case Network::Regtest:
      return protocol::Magic::Regtest;
    case Network::Signet:
      return protocol::Magic::Signet;
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

enum class HandshakeState {
  None,
  OneVersion,
  TwoVersions,
  OneVerack,
  Complete,
  Failed  
};

}  // namespace hornet::node::net

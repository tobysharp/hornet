// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.#pragma once
#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>

#include "hornetnodelib/net/constants.h"

namespace hornet::node::net {

struct PeerAddress {
  std::string host;
  uint16_t port = kMainnetPort;

  friend std::istream& operator>>(std::istream& in, PeerAddress& addr) {
    std::string input;
    in >> input;

    const auto bracket = input.find(']');
    const auto colon = (bracket != std::string::npos) ? input.find(':', bracket) : input.rfind(':');

    addr.host = (input.front() == '[' && bracket != std::string::npos)
                  ? input.substr(1, bracket - 1)
                  : input.substr(0, colon);

    if (colon != std::string::npos){ 
      const std::string port_str = input.substr(colon + 1);
      int port = std::stoi(port_str);
      if (port < 0 || port > 65535) 
        util::ThrowOutOfRange("Invalid port number: ", port_str);
      addr.port = static_cast<uint16_t>(port);
    }
    return in;
  }

  friend std::ostream& operator<<(std::ostream& out, const PeerAddress& addr) {
    if (addr.host.find(':') != std::string::npos) {
      out << '[' << addr.host << ']';
    } else {
      out << addr.host;
    }
    out << ':' << addr.port;
    return out;
  }
};

}  // namespace hornet::node::net
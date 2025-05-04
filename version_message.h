// version_message.h
#pragma once

#include "message.h"
#include "message_buffer.h"

#include <string>
#include <cstdint>
#include <array>
#include <vector>

class VersionMessage : public Message {
 public:
  int32_t version = 70015;
  uint64_t services = 0;
  int64_t timestamp = 0;
  uint64_t addr_recv_services = 0;
  std::array<uint8_t, 16> addr_recv_ip = {};
  uint16_t addr_recv_port = 8333;
  uint64_t addr_from_services = 0;
  std::array<uint8_t, 16> addr_from_ip = {};
  uint16_t addr_from_port = 8333;
  uint64_t nonce = 0;
  std::string user_agent = "/Hornet:0.1/";
  int32_t start_height = 0;
  bool relay = true;

  void Serialize(MessageBuffer& out) const override {
    out << version
        << services
        << static_cast<uint64_t>(timestamp)
        << addr_recv_services
        << addr_recv_ip
        << BigEndian(addr_recv_port)
        << addr_from_services
        << addr_from_ip
        << BigEndian(addr_from_port)
        << nonce
        << user_agent
        << start_height;

    if (version >= 70001) {
      out << relay;
    }
  }
};

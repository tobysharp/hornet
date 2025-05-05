// output_streamer.h
#pragma once

#include "hash.h"
#include "message_buffer.h"
#include "protocol.h"
#include "sha256.h"
#include "types.h"

#include <array>
#include <string>
#include <iostream>
#include <ostream>

class MessageBuilder {
 public:
  MessageBuilder(Magic magic = Magic::Testnet)
      : magic_(magic) {}

  MessageBuilder& operator<<(std::string_view command) {
    command_ = command;
    return *this;
  }

  MessageBuilder& operator<<(const Message& message) {
    buffer_.Add(static_cast<uint32_t>(magic_));

    // Write command (12 bytes, null-padded)
    std::array<char, 12> cmd = {};
    std::copy_n(command_.begin(), std::min(size_t{12}, command_.size()), cmd.begin());
    buffer_.Add(AsByteSpan<char>(cmd));

    // Defer payload length (4 bytes, LE)
    const auto payload_length_index = buffer_.Add(uint32_t{0});

    // Defer payload hash (4 bytes)
    const auto payload_hash_index = buffer_.Add(uint32_t{0});

    // Write payload itself
    const auto payload_start_index = buffer_.Size();
    message.Serialize(buffer_);

    // Compute payload length and write it into the buffer
    const auto payload_length_bytes = buffer_.Size() - payload_start_index;
    buffer_.WriteAt(payload_length_index, static_cast<uint32_t>(payload_length_bytes));
    
    // Compute payload hash and write it into the buffer
    const auto hash = DoubleSha256(std::span{buffer_.AsBytes().data() + payload_start_index, payload_length_bytes});
    buffer_.WriteAt(payload_hash_index, {hash.data(), 4});
    
    return *this;
  }

  auto AsBytes() const {
    return buffer_.AsBytes();
  }

  const Magic magic_;
  std::string command_;
  MessageBuffer buffer_;
};

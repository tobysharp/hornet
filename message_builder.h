// output_streamer.h
#pragma once

#include "hash.h"
#include "message_buffer.h"
#include "sha256.h"
#include "types.h"

#include <array>
#include <string>
#include <iostream>
#include <ostream>

class MessageBuilder {
 public:
  MessageBuilder(uint32_t magic)
      : magic_(magic) {}

  MessageBuilder& operator<<(std::string_view command) {
    command_ = command;
    return *this;
  }

  MessageBuilder& operator<<(const Message& message) {
    buffer_.Add(magic_);

    // Write command (12 bytes, null-padded)
    std::array<char, 12> cmd = {};
    std::copy_n(command_.begin(), std::min(size_t{12}, command_.size()), cmd.begin());
    buffer_.Add(AsByteSpan<char>({cmd.data(), cmd.size()}));

    // Store the location of the payload length buffer, so we can
    // go back and write in the value once it's known.
    const auto payload_length_index = buffer_.Size();

    // Write payload length (4 bytes, LE)
    buffer_.Add(uint32_t{0});

    bytes32_t hash = {};
    const auto payload_hash_index = buffer_.Size();
    buffer_.Add({hash.data(), 4});

    // Write payload itself
    const auto payload_start_index = buffer_.Size();
    message.Serialize(buffer_);

    // Compute payload length and write it into the buffer
    const auto payload_length_bytes = buffer_.Size() - payload_start_index;
    buffer_.WriteAt(payload_length_index, static_cast<uint32_t>(payload_length_bytes));
    
    // Compute payload hash and write it into the buffer
    hash = DoubleSha256(std::span{buffer_.AsBytes().data() + payload_start_index, payload_length_bytes});
    buffer_.WriteAt(payload_hash_index, {hash.data(), 4});
    
    return *this;
  }

  auto AsBytes() const {
    return buffer_.AsBytes();
  }

  const uint32_t magic_;
  std::string command_;
  MessageBuffer buffer_;
};

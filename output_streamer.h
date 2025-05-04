// output_streamer.h
#pragma once

#include "hash.h"
#include "message_buffer.h"
#include "sha256.h"

#include <array>
#include <string>
#include <ostream>

class OutputStreamer {
 public:
  OutputStreamer(std::ostream& output, uint32_t magic_bytes)
      : out_(output), magic_(magic_bytes) {}

  OutputStreamer& operator<<(const std::string& command) {
    current_command_ = command;
    command_set_ = true;
    return *this;
  }

  OutputStreamer& operator<<(std::span<const uint8_t> payload) {
    current_payload_ = payload;
    Flush();
    return *this;
  }

  void WriteMessage(const std::string& command, std::span<const uint8_t> payload) {
    current_command_ = command;
    current_payload_ = payload;
    Flush();
  }

 private:
  void Flush() {
    if (!command_set_ || current_payload_.empty()) return;

    // Magic
    out_.put(magic_ & 0xFF);
    out_.put((magic_ >> 8) & 0xFF);
    out_.put((magic_ >> 16) & 0xFF);
    out_.put((magic_ >> 24) & 0xFF);

    // Command (null-padded to 12 bytes)
    for (size_t i = 0; i < 12; ++i) {
      char c = (i < current_command_.size()) ? current_command_[i] : '\0';
      out_.put(c);
    }

    // Length
    uint32_t len = current_payload_.size();
    out_.put(len & 0xFF);
    out_.put((len >> 8) & 0xFF);
    out_.put((len >> 16) & 0xFF);
    out_.put((len >> 24) & 0xFF);

    // Checksum (first 4 bytes of double SHA256)
    auto checksum = DoubleSha256(current_payload_);
    out_.write(reinterpret_cast<char*>(checksum.data()), 4);

    // Payload
    out_.write(reinterpret_cast<const char*>(current_payload_.data()), current_payload_.size());

    current_command_.clear();
    command_set_ = false;
  }

  std::ostream& out_;
  uint32_t magic_;
  std::string current_command_;
  std::span<const uint8_t> current_payload_;
  bool command_set_ = false;
};

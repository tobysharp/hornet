// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

#include "hornetlib/crypto/hash.h"
#include "hornetlib/encoding/reader.h"
#include "hornetlib/protocol/constants.h"
#include "hornetlib/protocol/header.h"

namespace hornet::protocol {

class Parser {
 public:
  using Error = std::runtime_error;

  struct ParsedMessage {
    Header header;
    std::span<const uint8_t> payload;
  };

  explicit Parser(Magic expected_magic = Magic::Testnet) : magic_(expected_magic) {}

  // Reads a 24-byte header from a buffer.
  static Header ReadHeader(std::span<const uint8_t> buffer) {
    encoding::Reader reader{buffer};
    Header header;
    header.Deserialize(reader);
    return header;
  }

  // Parses (unframes) a buffer to extract command and payload.
  ParsedMessage Parse(std::span<const uint8_t> buffer) const {
    // Validate buffer holds enough data for header.
    if (buffer.size() < kHeaderLength) {
      throw Error("Message too short: requires 24-byte header.");
    }

    // Read the header.
    const Header header = ReadHeader(buffer);

    // Validate magic.
    if (header.magic != magic_) {
      throw Error("Invalid magic bytes.");
    }

    // Validate buffer length -- incomplete messages not allowed here.
    if (header.bytes > buffer.size() - kHeaderLength) {
      throw Error("Declared payload length exceeds buffer size.");
    } else if (header.bytes > kMaxMessageSize) {
      throw Error("Payload size exceeds protocol maximum.");
    }

    // Extract payload
    const auto payload = buffer.subspan(kHeaderLength, header.bytes);

    // Validate checksum
    const auto hash = crypto::DoubleSha256(payload);
    if (!std::equal(header.checksum.begin(), header.checksum.end(), hash.begin())) {
      throw Error("Checksum mismatch");
    }

    // Return unframed payload
    return {header, payload};
  }

  // Determines whether a buffer contains at least one complete message, ready
  // to be parsed and deserialized.
  bool IsCompleteMessage(std::span<const uint8_t> buffer) const {
    // Validate buffer holds enough data for header.
    if (buffer.size() < kHeaderLength) return false;

    // Read the header.
    const Header header = ReadHeader(buffer);

    // TODO: [HOR-19: Safeguard against long or garbage inbound messages]
    // (https://linear.app/hornet-node/issue/HOR-19/safeguard-against-long-or-garbage-inbound-messages)
    // Validate the header here. Check the magic bytes, the message name, and advertised message length.
    // Reject message if it's unknown, throw if it's too long.

    // Returns true if the buffer holds all the advertised data.
    return kHeaderLength + header.bytes <= buffer.size();
  }

  static ParsedMessage Parse(Magic expected_magic, std::span<const uint8_t> buffer) {
    return Parser{expected_magic}.Parse(buffer);
  }

 private:
  static inline std::string_view MapCommand(const std::array<char, 12>& bytes) {
    auto end = std::find(bytes.begin(), bytes.end(), 0);
    return std::string_view(bytes.data(), std::distance(bytes.begin(), end));
  }

  const Magic magic_;
};

inline Parser::ParsedMessage ParseMessage(Magic magic, const std::span<const uint8_t> buffer) {
  return Parser{magic}.Parse(buffer);
}

}  // namespace hornet::protocol

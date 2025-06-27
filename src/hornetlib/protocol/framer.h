// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hornetlib/crypto/hash.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/protocol/constants.h"
#include "hornetlib/protocol/header.h"
#include "hornetlib/protocol/message.h"

namespace hornet::protocol {

class Framer {
 public:
  explicit Framer(Magic magic = Magic::Testnet) : magic_(magic) {}

  void Frame(const Message& message) {
    // Defer writing the real header until after we know the payload details.
    const auto header_pos = writer_.GetPos();
    Header header = {.magic = magic_, .command = message.GetName()};
    header.Serialize(writer_);

    // Serialize the message payload.
    const auto payload_pos = writer_.GetPos();
    message.Serialize(writer_);
    header.bytes = writer_.GetPos() - payload_pos;

    // Compute the hash of the payload.
    const auto hash = crypto::DoubleSha256(std::span{Buffer().data() + payload_pos, header.bytes});
    std::copy_n(hash.begin(), header.checksum.size(), header.checksum.begin());

    // Rewind and serialize the correct header.
    const auto end_pos = writer_.SeekPos(header_pos);
    header.Serialize(writer_);
    writer_.SeekPos(end_pos);
  }

  const std::vector<uint8_t>& Buffer() const {
    return writer_.Buffer();
  }

  void Clear() {
    writer_.Clear();
  }

  static std::vector<uint8_t> FrameToBuffer(Magic magic, const Message& message) {
    Framer framer{magic};
    framer.Frame(message);
    return framer.writer_.ReleaseBuffer();
  }

 private:
  Magic magic_;
  encoding::Writer writer_;
};

inline std::vector<uint8_t> FrameMessage(Magic magic, const Message& message) {
  return Framer::FrameToBuffer(magic, message);
}

}  // namespace hornet::protocol

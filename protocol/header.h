#pragma once

#include <array>
#include <cstdint>

#include "encoding/reader.h"
#include "encoding/writer.h"
#include "protocol/constants.h"

namespace hornet::protocol {

struct Header {
  Magic magic = Magic::None;
  std::string command;
  uint32_t bytes = 0;
  std::array<uint8_t, kChecksumLength> checksum = {};

  void Serialize(encoding::Writer& w) const {
    w.WriteLE4(static_cast<uint32_t>(magic));
    w.WriteZeroPaddedString<kCommandLength>(command);
    w.WriteLE4(bytes);
    w.WriteBytes(checksum);
  }

  void Deserialize(encoding::Reader& r) {
    magic = static_cast<Magic>(r.ReadLE4());
    r.ReadZeroPaddedString<kCommandLength>(command);
    r.ReadLE4(bytes);
    r.ReadBytes(checksum);
  }
};

}  // namespace hornet::protocol

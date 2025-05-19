#pragma once

#include "protocol/constants.h"

namespace hornet::protocol {

class Capabilities {
 public:
  int GetVersion() const { return version_; }
  void SetVersion(int version) { version_ = version; }

  bool IsCompactBlocks() const { return compact_blocks_; }
  void SetCompactBlocks(bool compact = true) { compact_blocks_ = compact; }

 private:
  int version_ = kCurrentVersion;
  bool compact_blocks_ = false;
};

}  // namespace hornet::protocol

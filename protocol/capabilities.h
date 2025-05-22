#pragma once

#include "protocol/constants.h"

namespace hornet::protocol {

class Capabilities {
 public:
  int GetVersion() const { return version_; }
  void SetVersion(int version) { version_ = version; }

  int GetStartHeight() const { return start_height_; }
  void SetStartHeight(int height) { start_height_ = height; }

  bool IsCompactBlocks() const { return compact_blocks_; }
  void SetCompactBlocks(bool compact = true) { compact_blocks_ = compact; }

 private:
  int version_ = kCurrentVersion;
  int start_height_ = 0;
  bool compact_blocks_ = false;
};

}  // namespace hornet::protocol

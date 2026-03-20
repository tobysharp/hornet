// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "hornetlib/protocol/hash.h"
#include "hornetlib/util/assert.h"

namespace hornet::consensus {

namespace constants {
inline constexpr int kBlocksForMedianTime = 11;
}  // namespace constants

// Represents a read-only view onto the ancestors of a candidate block header.
// Height 0 corresponds to genesis. The highest accessible height is the parent
// of the header currently being validated.
class HeaderAncestryView {
 public:
  virtual ~HeaderAncestryView() = default;

  // Returns the length of the current chain.
  virtual int Length() const = 0;

  // Returns the hash of an ancestor block at the given height.
  virtual const protocol::Hash& HashAt(int height) const = 0;

  // Returns the timestamp of an ancestor at the given height.
  virtual uint32_t TimestampAt(int height) const = 0;

  // Returns the last `count` ancestor timestamps ending at the given height,
  // ordered from oldest to newest. May return fewer than `count` items if not all exist.
  virtual std::vector<uint32_t> LastNTimestamps(int height, int count) const = 0;

  // Returns the MTP (Median Time Past) of the tip ancestor at the given height.
  uint32_t MedianTimePast(int height) const {
    auto timestamps = LastNTimestamps(height, constants::kBlocksForMedianTime);
    Assert(!timestamps.empty());  // Impossible: would imply trying to validate the genesis.
    std::sort(timestamps.begin(), timestamps.end());
    return timestamps[timestamps.size() / 2];
  }

  // Returns the MTP (Median Time Past) of the tip.
  uint32_t MedianTimePast() const { return MedianTimePast(Length() - 1); }
};

}  // namespace hornet::consensus

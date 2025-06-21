// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace hornet::consensus {

// Represents a read-only view onto the ancestors of a candidate block header.
// Height 0 corresponds to genesis. The highest accessible height is the parent
// of the header currently being validated.
class HeaderAncestryView {
 public:
  virtual ~HeaderAncestryView() = default;

  // Returns the length of the current chain.
  virtual int Length() const = 0;

  // Returns the timestamp of an ancestor at the given height.
  virtual uint32_t TimestampAt(int height) const = 0;

  // Returns the last `count` ancestor timestamps ending at the current tip,
  // ordered from oldest to newest. Does not include the candidate.
  // May return fewer than `count` items if not all exist.
  virtual std::vector<uint32_t> LastNTimestamps(int count) const = 0;
};

}  // namespace hornet::consensus

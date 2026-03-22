// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <vector>

#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/protocol/hash.h"

namespace hornet::test {

class StubHeaderAncestryView : public consensus::HeaderAncestryView {
 public:
  int Length() const override { return 1; }
  const protocol::Hash& HashAt(int) const override { return hash_; }
  uint32_t TimestampAt(int) const override { return 0; }
  std::vector<uint32_t> LastNTimestamps(int, int) const override { return {0}; }

 private:
  protocol::Hash hash_{};
};

}  // namespace hornet::test
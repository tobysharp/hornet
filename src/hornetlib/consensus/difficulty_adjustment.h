// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <algorithm>
#include <cstdint>

#include "hornetlib/consensus/parameters.h"
#include "hornetlib/protocol/compact_target.h"
#include "hornetlib/protocol/target.h"
#include "hornetlib/util/big_uint.h"
#include "hornetlib/util/hex.h"

namespace hornet::consensus {

class DifficultyAdjustment {
 public:
  DifficultyAdjustment() {}
  DifficultyAdjustment(const Parameters& parameters) : parameters_(parameters) {}

  constexpr int GetBlocksPerPeriod() const { return parameters_.kAdjustmentInterval; }

  bool IsTransition(int height) const {
    return (height % parameters_.kAdjustmentInterval) == 0;
  }

  protocol::CompactTarget ComputeCompactTarget(int height, protocol::CompactTarget prev_bits,
                             uint32_t period_start_time, uint32_t period_end_time) const {
    if (!IsTransition(height)) return prev_bits;

    uint32_t period_duration = period_end_time - period_start_time;
    period_duration = std::clamp(period_duration, parameters_.kTargetDuration / 4, parameters_.kTargetDuration * 4);

    const protocol::Target last_target = prev_bits.Expand();
    const protocol::Target next_target = (last_target.Value() * period_duration) / parameters_.kTargetDuration;
    const protocol::Target limit_target = std::min(next_target, parameters_.kTargetLimit);
    return limit_target;
  }

 private:
  Parameters parameters_;
};

}  // namespace hornet::consensus

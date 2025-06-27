// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <algorithm>
#include <cstdint>

#include "hornetlib/consensus/parameters.h"
#include "hornetlib/protocol/target.h"
#include "hornetlib/util/big_uint.h"
#include "hornetlib/util/hex.h"

namespace hornet::consensus {

class DifficultyAdjustment {
 public:
  DifficultyAdjustment(const Parameters& parameters) : parameters_(parameters) {}

  constexpr int GetBlocksPerPeriod() const { return parameters_.kAdjustmentInterval; }

  bool IsTransition(int height) const {
    return (height % parameters_.kAdjustmentInterval) == 0;
  }

  uint32_t ComputeCompactTarget(int height, uint32_t prev_bits,
                             uint32_t period_start_time, uint32_t period_end_time) const {
    if (!IsTransition(height)) return prev_bits;

    uint32_t period_duration = period_end_time - period_start_time;
    period_duration = std::clamp(period_duration, parameters_.kTargetDuration / 4, parameters_.kTargetDuration * 4);

    const protocol::Target last_target = protocol::Target::FromCompact(prev_bits);
    const protocol::Target next_target =
        std::min((last_target.Value() * period_duration) / parameters_.kTargetDuration, parameters_.kTargetLimit.Value());
    return next_target.GetCompact();
  }

 private:
  Parameters parameters_;
};

}  // namespace hornet::consensus

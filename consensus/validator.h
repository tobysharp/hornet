// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <optional>
#include <variant>

#include "consensus/difficulty_adjustment.h"
#include "consensus/header_ancestry_view.h"
#include "data/header_context.h"
#include "protocol/block_header.h"
#include "protocol/hash.h"
#include "util/throw.h"

namespace hornet::consensus {

enum class HeaderError {
  None = 0,
  ParentNotFound,
  InvalidProofOfWork,
  BadTimestamp,
  BadDifficultyTransition
};

class Validator {
 public:
  struct Parameters {
    protocol::Hash genesis_hash;

    Parameters() : genesis_hash(protocol::kGenesisHash) {}
  };

  using HeaderResult = std::variant<data::HeaderContext, HeaderError>;

  Validator(const Parameters& params = {}) : parameters_(params) {}

  HeaderResult ValidateDownloadedHeader(const std::optional<data::HeaderContext>& parent,
                                        const protocol::BlockHeader& header,
                                        const HeaderAncestryView& view) const {
    const int height = parent ? parent->height + 1 : 0;

    // Verify previous hash
    if (height > 0 && parent->hash != header.GetPreviousBlockHash())
      return HeaderError::ParentNotFound;

    // Verify PoW target is a valid value
    if (!protocol::Target::FromCompact(header.GetCompactTarget()).IsValid())
      return HeaderError::InvalidProofOfWork;

    // Verify PoW target obeys the difficulty adjustment rules.
    const uint32_t period_end_time = parent->header.GetTimestamp();  // block[height - 1].time
    const auto blocks_per_period = difficulty_adjustment_.GetBlocksPerPeriod();
    const uint32_t period_start_time =
        (height >= blocks_per_period) ? *view.TimestampAt(height - blocks_per_period) : 0;
    const uint32_t expected_bits = difficulty_adjustment_.ComputeCompactTarget(
        height, parent->header.GetCompactTarget(), period_start_time, period_end_time);
    if (expected_bits != header.GetCompactTarget()) return HeaderError::BadDifficultyTransition;

    // Verify that the hash achieves the target PoW value.
    if (!header.IsProofOfWork()) return HeaderError::InvalidProofOfWork;

    // TODO: Verify the version number?
    // TODO: Verify the timestamp matches the MTP rule.

    // TODO: In Core, a previous block could be marked as failed/bad, in which case this one is
    // invalid too.
    return parent ? parent->Extend(header) : data::HeaderContext::Genesis(header);
  }

 private:
  Parameters parameters_;
  DifficultyAdjustment difficulty_adjustment_;
};

}  // namespace hornet::consensus

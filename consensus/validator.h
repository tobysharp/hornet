#pragma once

#include <optional>

#include "consensus/header_ancestry_view.h"
#include "consensus/difficulty_adjustment.h"
#include "data/header_context.h"
#include "protocol/block_header.h"
#include "protocol/hash.h"
#include "util/throw.h"

namespace hornet::consensus {

class Validator {
 public:
  struct Parameters {
    protocol::Hash genesis_hash;
    
    Parameters() : genesis_hash(protocol::kGenesisHash) {}
  };

  Validator(const Parameters& params = {}) : parameters_(params) {}

  std::optional<data::HeaderContext> ValidateDownloadedHeader(
      const std::optional<data::HeaderContext>& parent, const protocol::BlockHeader& header,
      const HeaderAncestryView& view) const {
    const int height = parent ? parent->height + 1 : 0;
    
    // Verify previous hash
    if (height > 0 && parent->hash != header.GetPreviousBlockHash()) return {};

    // Verify PoW target is a valid value
    if (!protocol::Target::FromCompact(header.GetCompactTarget()).IsValid()) return {};

    // Verify PoW target obeys the difficulty adjustment rules.
    const uint32_t period_end_time = parent->header.GetTimestamp();  // block[height - 1].time
    const auto blocks_per_period = difficulty_adjustment_.GetBlocksPerPeriod();
    const uint32_t period_start_time =
        (height >= blocks_per_period) ? *view.TimestampAt(height - blocks_per_period) : 0;
    const uint32_t expected_bits = difficulty_adjustment_.ComputeCompactTarget(
        height, parent->header.GetCompactTarget(), period_start_time, period_end_time);
    if (expected_bits != header.GetCompactTarget()) return {};

    // Verify that the hash achieves the target PoW value.
    if (!header.IsProofOfWork()) return {};

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

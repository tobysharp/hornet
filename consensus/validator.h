#pragma once

#include <optional>

#include "consensus/difficulty_adjustment.h"
#include "consensus/header_context.h"
#include "protocol/block_header.h"
#include "protocol/hash.h"
#include "util/throw.h"

namespace hornet::consensus {

class Validator {
 public:
  class Parameters {
   public:
    const protocol::Hash& GetGenesisHash() const {
      return genesis_hash_;
    }

   private:
    protocol::Hash genesis_hash_ = protocol::kGenesisHash;
  };

  Validator(const Parameters&);

  std::optional<HeaderContext> ValidateDownloadedHeader(
      const std::optional<HeaderContext>& parent, const protocol::BlockHeader& header) const {
    const int height = parent ? parent->height + 1 : 0;
    // Verify previous hash
    if (height > 0) {
      const protocol::Hash& parent_hash = header.GetPreviousBlockHash();
      if (parent->hash != parent_hash) return {};
    }

    // Verify PoW target is a valid value
    if (!protocol::Target::FromCompact(header.GetCompactTarget()).IsValid()) return {};

    // Verify PoW target obeys the difficulty adjustment rules.
    const uint32_t period_end_time = parent->header.GetTimestamp();  // block[height - 1].time
    const auto blocks_per_period = difficulty_adjustment_.GetBlocksPerPeriod();
    const uint32_t period_start_time =
        height >= blocks_per_period ? 0 : 0;  // TODO: block[height - blocks_per_period].time
    const uint32_t expected_bits = difficulty_adjustment_.ComputeCompactTarget(
        height, parent->header.GetCompactTarget(), period_start_time, period_end_time);
    if (expected_bits != header.GetCompactTarget()) return {};

    // Verify that the hash achieves the target PoW value.
    if (!header.IsProofOfWork()) return {};

    // TODO: Verify the version number?
    // TODO: Verify the timestamp matches the MTP rule.

    // TODO: In Core, a previous block could be marked as failed/bad, in which case this one is
    // invalid too.
    return parent ? parent->Extend(header) : HeaderContext::Genesis(header);
  }

 private:
  Parameters parameters_;
  DifficultyAdjustment difficulty_adjustment_;
};

}  // namespace hornet::consensus

#pragma once

#include <optional>

#include "consensus/difficulty_adjustment.h"
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

  bool IsDownloadedHeaderValid(const protocol::BlockHeader& header, int height,
                               const std::optional<protocol::BlockHeader>& tip) const {
    // Verify previous hash
    if (height > 0) {
      if (!tip) util::ThrowRuntimeError("Previous header not provided to validation.");
      const protocol::Hash& tip_hash = tip->GetHash();
      const protocol::Hash& parent_hash = header.GetPreviousBlockHash();
      if (tip_hash != parent_hash) return false;
    }

    // Verify PoW target is a valid value
    if (!protocol::Target::FromCompact(header.GetCompactTarget()).IsValid()) return false;
  
    // Verify PoW target obeys the difficulty adjustment rules.
    const uint32_t period_end_time = tip->GetTimestamp();  // block[height - 1].time
    const auto blocks_per_period = difficulty_adjustment_.GetBlocksPerPeriod();
    const uint32_t period_start_time = height >= blocks_per_period ? 0 : 0;  // TODO: block[height - blocks_per_period].time
    const uint32_t expected_bits = difficulty_adjustment_.ComputeCompactTarget(
        height, tip->GetCompactTarget(), period_start_time, period_end_time);
    if (expected_bits != header.GetCompactTarget()) return false;

    // Verify that the hash achieves the target PoW value.
    if (!header.IsProofOfWork()) return false;

    // TODO: Verify the version number?
    // TODO: Verify the timestamp matches the MTP rule.

    // TODO: In Core, a previous block could be marked as failed/bad, in which case this one is invalid too.
  }

 private:
  Parameters parameters_;
  DifficultyAdjustment difficulty_adjustment_;
};

}  // namespace hornet::consensus

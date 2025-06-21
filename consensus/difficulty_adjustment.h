// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <algorithm>
#include <cstdint>

#include "consensus/parameters.h"
#include "protocol/target.h"
#include "util/big_uint.h"
#include "util/hex.h"

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
    if (IsTransition(height)) return prev_bits;

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

// class DifficultyAdjustment {
//  public:
//   virtual uint32_t GetNextCompactTarget(/*last block, current header, consensus params */)
//   const; virtual bool IsValidAdjustment(
//       /* consensus params, block height, last bits, current bits */) const;

//  protected:
//   DifficultyAdjustment();
// };
/*
  In Bitcoin Core, the consensus parameters used in src/pow.cpp for difficulty adjustments are:
  Consensus::Params members:

  ChainType::                       MAIN        TESTNET        TESTNET4       SIGNET       REGTEST
  fPowAllowMinDifficultyBlocks      false        true           true          false         true
  enforce_BIP94                     false        false          true          false       optional
  nPowTargetSpacing                 10*60        10*60          10*60         10*60         10*60
  nPowTargetTimespan            14*24*60*60    14*24*60*60    14*24*60*60   14*24*60*60   24*60*60
  fPowNoRetargeting                 false        false          false         false         true
  powLimit                       2^224 - 1      2^224 - 1      2^224 - 1    227246*2^216   2^255-1

  {false, false, 10 mins, 2 weeks, false, ?}:       MAIN, SIGNET
  {true,  ?,     10 mins, 2 weeks, false, 2^224-1}: TESTNET, TESTNET4
  {true,  ?,     10 mins, 1 day,   true,  2^255-1}: REGTEST


*/
/*
template <typename BoundaryDifficultyPolicy, typename OffBoundaryDifficultyPolicy>
class DifficultyAdjustment {
public:
    uint32_t GetNextWorkRequired(const BlockIndex* last, const BlockHeader* next) const {
        assert(last != nullptr);
        if ((last->nHeight + 1) % kAdjustmentInterval != 0) {
            return OffBoundaryPolicy::GetNextWorkRequired(last, next);
        } else {
            return BoundaryPolicy::GetNextWorkRequired(last, next);
        }
    }

private:
    static constexpr int kAdjustmentInterval = 2016;

    using BoundaryPolicy = BoundaryDifficultyPolicy;
    using OffBoundaryPolicy = OffBoundaryDifficultyPolicy;
};

template <int TargetTimespan, int TargetSpacing = 600>
inline uint32_t CalculateNextWorkRequired(
    const BlockIndex& last,
    int64_t firstBlockTime,
    const arith_uint256& powLimit
) {
    int64_t actual = last.GetBlockTime() - firstBlockTime;
    actual = std::clamp(actual, TargetTimespan / 4, TargetTimespan * 4);

    arith_uint256 target;
    target.SetCompact(last.nBits);
    target *= actual;
    target /= TargetTimespan;

    if (target > powLimit)
        target = powLimit;

    return target.GetCompact();
}

struct StandardDifficultyAdjustmentPolicy {
    static uint32_t GetNextWorkRequired(const BlockIndex* last, const BlockHeader*) {
        constexpr int kTimespan = 14 * 24 * 60 * 60;
        constexpr arith_uint256 kPowLimit = UintToArith256(MAINNET_POW_LIMIT);
        const BlockIndex* first = last->GetAncestor(last->nHeight - 2015);
        assert(first);
        return CalculateNextWorkRequired<kTimespan>(*last, first->GetBlockTime(), kPowLimit);
    }
};

struct NoRetargetingDifficultyPolicy {
    static uint32_t GetNextWorkRequired(const BlockIndex* last, const BlockHeader*) {
        return last->nBits;
    }
};

template <uint32_t kPowLimitCompact, int kTargetSpacing = 600>
struct MinimumDifficultyPolicy {
    static uint32_t GetNextWorkRequired(const BlockIndex* last, const BlockHeader* next) {
        if (next->GetBlockTime() > last->GetBlockTime() + 2 * kTargetSpacing)
            return kPowLimitCompact;

        const BlockIndex* p = last;
        while (p->pprev && p->nHeight % 2016 != 0 && p->nBits == kPowLimitCompact)
            p = p->pprev;

        return p->nBits;
    }
};

using MainnetDifficulty = DifficultyAdjustment<
    StandardDifficultyAdjustmentPolicy,
    NoRetargetingDifficultyPolicy
>;

using TestnetDifficulty = DifficultyAdjustment<
    StandardDifficultyAdjustmentPolicy,
    MinimumDifficultyPolicy<TESTNET_POW_LIMIT_COMPACT>
>;

using RegtestDifficulty = DifficultyAdjustment<
    NoRetargetingDifficultyPolicy,
    MinimumDifficultyPolicy<REGTEST_POW_LIMIT_COMPACT>
>;

template <>
struct NetworkTraits<kMain> {
    using Difficulty = MainnetDifficulty;
};

template <>
struct NetworkTraits<kTestnet> {
    using Difficulty = TestnetDifficulty;
};

template <>
struct NetworkTraits<kTestnet4> {
    using Difficulty = TestnetDifficulty;
};

template <>
struct NetworkTraits<kSignet> {
    using Difficulty = MainnetDifficulty;
};

template <>
struct NetworkTraits<kRegTest> {
    using Difficulty = RegtestDifficulty;
};

template <Network N>
uint32_t GetNextWorkRequired(const BlockIndex* last, const BlockHeader* next) {
    static const typename NetworkTraits<N>::Difficulty difficulty;
    return difficulty.GetNextWorkRequired(last, next);
}

*/

}  // namespace hornet::consensus

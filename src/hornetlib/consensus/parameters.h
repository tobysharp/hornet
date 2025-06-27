#pragma once

#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/target.h"
#include "hornetlib/util/hex.h"

namespace hornet::consensus {

struct Parameters {
  static constexpr protocol::Hash kGenesisHash = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_h;
  static constexpr int kAdjustmentInterval = 2016;               // Blocks per difficulty period
  static constexpr int kBIP34Height = 227931;
  static constexpr int kBIP65Height = 388381;
  static constexpr int kBIP66Height = 363725;
  static constexpr int kBlocksForMedianTime = 11;
  static constexpr int kTimestampTolerance = 2 * 60 * 60;
  static constexpr uint32_t kTargetDuration = 14 * 24 * 60 * 60;  // Two weeks in seconds
  static constexpr protocol::Target kTargetLimit = "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"_h256;
};

}  // namespace hornet::consensus

#include "hornetlib/consensus/difficulty_adjustment.h"

#include <gtest/gtest.h>

namespace hornet::consensus {
namespace {

using protocol::CompactTarget;

// Constants replicated from DifficultyAdjustment for testing
constexpr int kBlocksPerDifficultyPeriod = 2016;
constexpr int64_t kDifficultyPeriodDuration = 14 * 24 * 60 * 60;  // Two weeks
constexpr protocol::Target kPoWTargetLimit =
    "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"_h256;

TEST(DifficultyAdjustmentTest, ReturnsPrevBitsWhenHeightNotBoundary) {
  constexpr CompactTarget prev_bits = 0x1d00ffff;
  EXPECT_EQ(ComputeCompactTarget(1000, prev_bits, 0, 0), prev_bits);
}

TEST(DifficultyAdjustmentTest, UnchangedWhenPeriodMatchesTarget) {
  const int height = kBlocksPerDifficultyPeriod;
  constexpr CompactTarget prev_bits = 0x1d00ffff;

  const auto result =
      ComputeCompactTarget(height, prev_bits, 0, kDifficultyPeriodDuration);
  EXPECT_EQ(result, prev_bits);
}

TEST(DifficultyAdjustmentTest, AdjustsDownWhenBlocksFaster) {
  const int height = constants::kBlocksPerDifficultyPeriod;
  constexpr CompactTarget prev_bits = 0x1d00ffff;
  const int64_t end_time = kDifficultyPeriodDuration / 2;
  const auto result =
      ComputeCompactTarget(height, prev_bits, 0, end_time);

  const protocol::Target last_target = prev_bits.Expand();
  const int64_t clamped =
      std::clamp(end_time, kDifficultyPeriodDuration / 4, kDifficultyPeriodDuration * 4);
  const protocol::Target expected_target =
      std::min((last_target.Value() * clamped) / kDifficultyPeriodDuration,
               kPoWTargetLimit.Value());
  const CompactTarget expected_bits = expected_target;
  EXPECT_EQ(result, expected_bits);
}

TEST(DifficultyAdjustmentTest, CapsAtTargetLimit) {
  const int height = constants::kBlocksPerDifficultyPeriod;
  constexpr CompactTarget prev_bits = 0x1d00eeff;  // Near the limit
  const auto result =
      ComputeCompactTarget(height, prev_bits, 0, kDifficultyPeriodDuration * 10);

  const protocol::Target last_target = prev_bits.Expand();
  const int64_t clamped =
      std::clamp(kDifficultyPeriodDuration * 10u, kDifficultyPeriodDuration / 4, kDifficultyPeriodDuration * 4);
  const protocol::Target expected_target =
      std::min((last_target.Value() * clamped) / kDifficultyPeriodDuration,
               kPoWTargetLimit.Value());
  const CompactTarget expected_bits = expected_target;
  EXPECT_EQ(result, expected_bits);
}

}  // namespace
}  // namespace hornet::consensus

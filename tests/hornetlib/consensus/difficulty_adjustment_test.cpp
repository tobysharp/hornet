#include "hornetlib/consensus/difficulty_adjustment.h"

#include <gtest/gtest.h>

namespace hornet::consensus {
namespace {

using protocol::CompactTarget;

// Constants replicated from DifficultyAdjustment for testing
constexpr uint32_t kTargetDuration = 14 * 24 * 60 * 60;  // Two weeks
constexpr protocol::Target kTargetLimit =
    "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"_h256;

TEST(DifficultyAdjustmentTest, ReturnsPrevBitsWhenHeightNotBoundary) {
  DifficultyAdjustment da;
  constexpr CompactTarget prev_bits = 0x1d00ffff;
  EXPECT_EQ(da.ComputeCompactTarget(1000, prev_bits, 0, 0), prev_bits);
}

TEST(DifficultyAdjustmentTest, UnchangedWhenPeriodMatchesTarget) {
  DifficultyAdjustment da;
  const uint32_t height = da.GetBlocksPerPeriod();
  constexpr CompactTarget prev_bits = 0x1d00ffff;

  const auto result =
      da.ComputeCompactTarget(height, prev_bits, 0, kTargetDuration);
  EXPECT_EQ(result, prev_bits);
}

TEST(DifficultyAdjustmentTest, AdjustsDownWhenBlocksFaster) {
  DifficultyAdjustment da;
  const uint32_t height = da.GetBlocksPerPeriod();
  constexpr CompactTarget prev_bits = 0x1d00ffff;
  const uint32_t end_time = kTargetDuration / 2;
  const auto result =
      da.ComputeCompactTarget(height, prev_bits, 0, end_time);

  const protocol::Target last_target = prev_bits.Expand();
  const uint32_t clamped =
      std::clamp(end_time, kTargetDuration / 4, kTargetDuration * 4);
  const protocol::Target expected_target =
      std::min((last_target.Value() * clamped) / kTargetDuration,
               kTargetLimit.Value());
  const CompactTarget expected_bits = expected_target;
  EXPECT_EQ(result, expected_bits);
}

TEST(DifficultyAdjustmentTest, CapsAtTargetLimit) {
  DifficultyAdjustment da;
  const uint32_t height = da.GetBlocksPerPeriod();
  constexpr CompactTarget prev_bits = 0x1d00eeff;  // Near the limit
  const auto result =
      da.ComputeCompactTarget(height, prev_bits, 0, kTargetDuration * 10);

  const protocol::Target last_target = prev_bits.Expand();
  const uint32_t clamped =
      std::clamp(kTargetDuration * 10u, kTargetDuration / 4, kTargetDuration * 4);
  const protocol::Target expected_target =
      std::min((last_target.Value() * clamped) / kTargetDuration,
               kTargetLimit.Value());
  const CompactTarget expected_bits = expected_target;
  EXPECT_EQ(result, expected_bits);
}

}  // namespace
}  // namespace hornet::consensus

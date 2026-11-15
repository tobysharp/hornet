// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/consensus/rules/validate_header.h"

#include <chrono>
#include <cstdint>
#include <limits>

#include "hornetlib/consensus/rules/context.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/consensus/stub_header_ancestry_view.h"

#include <gtest/gtest.h>

namespace hornet::consensus {
namespace {

using namespace std::chrono_literals;
using rules::HeaderValidationContext;
using rules::ValidateTimestampCurrent;
using test::StubHeaderAncestryView;

// Regression tests for the future-timestamp rule (Bitcoin Core's MAX_FUTURE_BLOCK_TIME).
// These pin the boundary semantics using explicitly typed epoch seconds. The live validation
// paths must also supply seconds: passing milliseconds previously disabled this rule there.

class ValidateTimestampCurrentTest : public ::testing::Test {
 protected:
  // A fixed epoch "now" in seconds, comfortably within uint32_t header-timestamp range.
  static constexpr auto kNow = std::chrono::seconds{1'760'000'000};

  Result ValidateAt(uint32_t header_timestamp, std::chrono::seconds now = kNow) {
    header_.SetTimestamp(header_timestamp);
    const HeaderValidationContext context{header_, parent_, view_, now, int{view_.Length()}};
    return ValidateTimestampCurrent(context);
  }

  protocol::BlockHeader header_;
  protocol::BlockHeader parent_;
  StubHeaderAncestryView view_;
};

TEST_F(ValidateTimestampCurrentTest, CurrentTimestampAccepted) {
  EXPECT_TRUE(ValidateAt(uint32_t(kNow.count())));
}

TEST_F(ValidateTimestampCurrentTest, PastTimestampAccepted) {
  EXPECT_TRUE(ValidateAt(uint32_t((kNow - std::chrono::days{1}).count())));
}

TEST_F(ValidateTimestampCurrentTest, ExactlyTwoHoursInFutureAccepted) {
  // The bound is strict: a timestamp of exactly now + 2h is permitted.
  EXPECT_TRUE(ValidateAt(uint32_t((kNow + std::chrono::hours{2}).count())));
}

TEST_F(ValidateTimestampCurrentTest, BeyondTwoHoursInFutureRejected) {
  EXPECT_EQ(ValidateAt(uint32_t((kNow + std::chrono::hours{2} + 1s).count())),
            Error::Header_BadTimestamp);
}

TEST_F(ValidateTimestampCurrentTest, FarFutureTimestampRejected) {
  // The live validation paths previously accepted far-future timestamps because their
  // current_time values were in milliseconds rather than seconds.
  EXPECT_EQ(ValidateAt(uint32_t((kNow + std::chrono::days{30}).count())),
            Error::Header_BadTimestamp);
}

TEST_F(ValidateTimestampCurrentTest, MaxTimestampRejected) {
  EXPECT_EQ(ValidateAt(std::numeric_limits<uint32_t>::max()), Error::Header_BadTimestamp);
}

}  // namespace
}  // namespace hornet::consensus

#include "hornetlib/consensus/algebra.h"

#include <numbers>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace hornet::consensus::algebra {
namespace {

Error Error_NotPi = static_cast<Error>(1);
Error Error_Negative = static_cast<Error>(2);
Error Error_TooBig = static_cast<Error>(3);

Result ExpectPi(float f) {
  if (f != std::numbers::pi_v<float>) return Error_NotPi;
  return {};
}

Result ExpectBelow100(int x) {
  return x >= 100 ? Error_TooBig : Result::Ok;
}

Result ExpectNonNegative(float f) {
  if (f < 0.0f) return Error_Negative;
  return {};
}

TEST(AlgebraTest, SingleRuleNode) {
  static constexpr auto kGraph = Rule{ExpectPi};
  EXPECT_TRUE(Validate(kGraph, std::numbers::pi_v<float>));
  EXPECT_EQ(Validate(kGraph, 1.0f), Error_NotPi);
}

TEST(AlgebraTest, MultipleRuleNodes) {
  static constexpr auto kGraph = All{
    Rule{ExpectNonNegative},
    Rule{ExpectPi}
  };
  EXPECT_EQ(Validate(kGraph, -1.0f), Error_Negative);
  EXPECT_EQ(Validate(kGraph, 1.0f), Error_NotPi);
  EXPECT_TRUE(Validate(kGraph, std::numbers::pi_v<float>));
}

TEST(AlgebraTest, ProjectorNode) {
  const auto int_to_float = [](int x) { return (x >= 0) * std::numbers::pi_v<float>; };
  static constexpr auto kGraph = With{int_to_float, Rule{ExpectPi}};
  EXPECT_FALSE(Validate(kGraph, -1));
  EXPECT_TRUE(Validate(kGraph, 1));
}

TEST(AlgebraTest, IteratorNode) {
  std::vector<float> values = { 1.0f, 2.0f, -3.0f };
  static constexpr auto kGraph1 = Each{Rule{ExpectNonNegative}};
  static constexpr auto kGraph2 = Each{Rule{ExpectPi}};
  EXPECT_FALSE(Validate(kGraph1, values));
  EXPECT_FALSE(Validate(kGraph2, values));
}

TEST(AlgebraTest, WhenNode) {
  const auto is_odd = [](int i) -> bool { return i & 1; };
  EXPECT_TRUE(Validate(When{is_odd, Rule{ExpectBelow100}}, 1));
  EXPECT_TRUE(Validate(When{is_odd, Rule{ExpectBelow100}}, 200));
  EXPECT_FALSE(Validate(When{is_odd, Rule{ExpectBelow100}}, 201));
}

struct IsBIPActive {
  BIP bip;
  template <typename Context>
  bool operator()(const Context& context) const {
    return IsBIPActiveAtHeight(bip, GetHeight(context));
  }
};

template <typename Context>
int GetHeight(const Context& context) {
  return context.height;
}

struct FloatContext {
  float x;
  int height;
};

struct MakeX {
  float operator()(const FloatContext& context) const { return context.x; }
};

struct FloatRangeContext {
  std::vector<float> values;
};

struct MakeValues {
  const std::vector<float>& operator()(const FloatRangeContext& context) const {
    return context.values;
  }
};

struct FloatRangeHeightContext {
  std::vector<float> values;
  int height;
};

struct MakeHeightValues {
  const std::vector<float>& operator()(const FloatRangeHeightContext& context) const {
    return context.values;
  }
};

template <typename Child>
constexpr auto From(BIP bip, Child&& child) {
  return When{IsBIPActive{bip}, std::forward<Child>(child)};
}

TEST(AlgebraTest, FromNode) {
  static constexpr auto kGraph = From(BIP::SegWitV0, With{MakeX{}, Rule{ExpectPi}});
  EXPECT_TRUE(Validate(kGraph, FloatContext{std::numbers::pi_v<float>, 1'000'000}));
  EXPECT_TRUE(Validate(kGraph, FloatContext{0.0f, 1}));
  EXPECT_FALSE(Validate(kGraph, FloatContext{0.0f, 1'000'000}));
}

TEST(AlgebraTest, CompositeProjectedIteratorNode) {
  static constexpr auto kGraph = With{
    MakeValues{},
    Each{All{
      Rule{ExpectNonNegative},
      Rule{ExpectPi},
    }},
  };

  EXPECT_TRUE(Validate(kGraph, FloatRangeContext{{
    std::numbers::pi_v<float>,
    std::numbers::pi_v<float>,
  }}));
  EXPECT_EQ(Validate(kGraph, FloatRangeContext{{
    -std::numbers::pi_v<float>,
    std::numbers::pi_v<float>,
  }}), Error_Negative);
  EXPECT_EQ(Validate(kGraph, FloatRangeContext{{
    std::numbers::pi_v<float>,
    1.0f,
  }}), Error_NotPi);
}

TEST(AlgebraTest, CompositeGatedIteratorNode) {
  static constexpr auto kGraph = From(
    BIP::SegWitV0,
    With{MakeHeightValues{}, Each{Rule{ExpectNonNegative}}});

  EXPECT_TRUE(Validate(kGraph, FloatRangeHeightContext{{1.0f, -2.0f}, 1}));
  EXPECT_TRUE(Validate(kGraph, FloatRangeHeightContext{{1.0f, 2.0f}, 1'000'000}));
  EXPECT_EQ(Validate(kGraph, FloatRangeHeightContext{{1.0f, -2.0f}, 1'000'000}), Error_Negative);
}

}  // namespace
}  // namespace hornet::consensus::algebra
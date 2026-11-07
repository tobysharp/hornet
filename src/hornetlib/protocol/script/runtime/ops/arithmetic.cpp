// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include <cstdint>
#include <functional>

#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"

namespace hornet::protocol::script::runtime {
namespace {

template <auto Fn, typename T> void CallUnary(const Context& context) {
  context.Call([&](int32_t arg) { return Fn(static_cast<T>(arg)); });
}

template <auto Fn, typename T> void CallBinary(const Context& context) {
  context.Call([&](int32_t lhs, int32_t rhs) { return Fn(static_cast<T>(lhs), static_cast<T>(rhs)); });
}

template <auto Fn> constexpr auto OnUnaryBool = &CallUnary<Fn, bool>;
template <auto Fn> constexpr auto OnUnaryInt = &CallUnary<Fn, int64_t>;
template <auto Fn> constexpr auto OnBinaryBool = &CallBinary<Fn, bool>;
template <auto Fn> constexpr auto OnBinaryInt = &CallBinary<Fn, int64_t>;

constexpr int64_t Abs(int64_t a) { return std::abs(a); }
constexpr int64_t Increment(int64_t a) { return ++a; }
constexpr int64_t Decrement(int64_t a) { return --a; }
constexpr int64_t Min(int64_t a, int64_t b) { return std::min(a, b); }
constexpr int64_t Max(int64_t a, int64_t b) { return std::max(a, b); }

void OnNumEqualVerify(const Context& context) {
  context.Call([&](int32_t lhs, int32_t rhs) {
    return lhs == rhs;
  });
  context.Verify(lang::Error::NumEqualVerify);
}

void OnWithin(const Context& context) {
  context.Call([&](int32_t x, int32_t xmin, int32_t xmax) {
    return xmin <= x && x < xmax;
  });
}

}  // namespace

// Register handlers
void RegisterArithmeticHandlers(Dispatcher& table) {
  using lang::Op;
  // clang-format off
  table[Op::Abs]                = OnUnaryInt  <Abs>;
  table[Op::Add]                = OnBinaryInt <std::plus{}>;
  table[Op::BooleanAnd]         = OnBinaryBool<std::logical_and{}>;
  table[Op::BooleanOr]          = OnBinaryBool<std::logical_or{}>;
  table[Op::Decrement]          = OnUnaryInt  <Decrement>;
  table[Op::GreaterThan]        = OnBinaryInt <std::greater{}>;
  table[Op::GreaterThanOrEqual] = OnBinaryInt <std::greater_equal{}>;
  table[Op::Increment]          = OnUnaryInt  <Increment>;
  table[Op::IsZero]             = OnUnaryBool <std::logical_not{}>;
  table[Op::IsNonZero]          = OnUnaryBool <std::identity{}>;
  table[Op::LessThan]           = OnBinaryInt <std::less{}>;
  table[Op::LessThanOrEqual]    = OnBinaryInt <std::less_equal{}>;
  table[Op::Min]                = OnBinaryInt <Min>;
  table[Op::Max]                = OnBinaryInt <Max>;
  table[Op::Negate]             = OnUnaryInt  <std::negate{}>;
  table[Op::NumEqual]           = OnBinaryInt <std::equal_to{}>;
  table[Op::NumEqualVerify]     = &OnNumEqualVerify;
  table[Op::NumNotEqual]        = OnBinaryInt <std::not_equal_to{}>;
  table[Op::Subtract]           = OnBinaryInt <std::minus{}>;
  table[Op::Within]             = &OnWithin;
}  // clang-format on

}  // namespace hornet::protocol::script::runtime

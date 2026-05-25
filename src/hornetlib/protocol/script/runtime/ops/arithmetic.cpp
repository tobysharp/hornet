// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"

namespace hornet::protocol::script::runtime {
namespace {

using lang::Bytes;

template <auto Fn, typename T = int64_t> auto BinaryInt32(const Machine& machine, Bytes lhs, Bytes rhs) {
  const T a = static_cast<T>(machine.DecodeInt32(lhs));
  const T b = static_cast<T>(machine.DecodeInt32(rhs));
  return Fn(a, b);
}

template <auto Fn, typename T> void CallUnary(const Context& context) {
  context.Call([&](Bytes arg) { return Fn(static_cast<T>(context.machine.DecodeInt32(arg))); });
}

template <auto Fn, typename T> void CallBinary(const Context& context) {
  context.Call([&](Bytes lhs, Bytes rhs) { return BinaryInt32<Fn, T>(context.machine, lhs, rhs); });
}

template <auto Fn> constexpr auto OnUnaryInt = &CallUnary<Fn, int64_t>;
template <auto Fn> constexpr auto OnUnaryBool = &CallUnary<Fn, bool>;
template <auto Fn> constexpr auto OnBinaryBool = &CallBinary<Fn, bool>;
template <auto Fn> constexpr auto OnBinaryInt = &CallBinary<Fn, int64_t>;

constexpr int64_t Abs(int64_t a) { return std::abs(a); }
constexpr int64_t Increment(int64_t a) { return ++a; }
constexpr int64_t Decrement(int64_t a) { return --a; }

constexpr int64_t Min(int64_t a, int64_t b) { return std::min(a, b); }
constexpr int64_t Max(int64_t a, int64_t b) { return std::max(a, b); }

void OnNumEqualVerify(const Context& context) {
  context.Call([&](Bytes lhs, Bytes rhs) {
    if (!BinaryInt32<std::equal_to{}>(context.machine, lhs, rhs)) Throw(lang::Error::NumEqualVerify);
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
}  // clang-format on

}  // namespace hornet::protocol::script::runtime

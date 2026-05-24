// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"
#include "hornetlib/protocol/script/runtime/throw.h"

namespace hornet::protocol::script::runtime {

using lang::Bytes;
using lang::Op;

namespace {

template <auto Fn, typename T = int64_t>
auto BinaryInt32(const Machine& machine, Bytes lhs, Bytes rhs) {
  const T a = static_cast<T>(machine.DecodeInt32(lhs));
  const T b = static_cast<T>(machine.DecodeInt32(rhs));
  return Fn(a, b);
}

template <auto Fn, typename T> 
void CallBinary(const Context& context) {
  context.Call([&](Bytes lhs, Bytes rhs) {
    return BinaryInt32<Fn, T>(context.machine, lhs, rhs);
  });
}

template <auto Fn>
void OnBinaryInt(const Context& context) {
  CallBinary<Fn, int64_t>(context);
}

template <auto Fn>
void OnBinaryBool(const Context& context) {
  CallBinary<Fn, bool>(context);
}

void OnNumEqualVerify(const Context& context) {
  context.Call([&](Bytes lhs, Bytes rhs) {
    if (!BinaryInt32<std::equal_to{}>(context.machine, lhs, rhs)) Throw(lang::Error::NumEqualVerify);
  });
}

constexpr auto kMin = [](auto a, auto b) { return std::min(a, b); };
constexpr auto kMax = [](auto a, auto b) { return std::max(a, b); };

}

// Register handlers
void RegisterArithmeticHandlers(Dispatcher& table) {
  table[Op::Add] = &OnBinaryInt<std::plus{}>;
  table[Op::BooleanAnd] = &OnBinaryBool<std::logical_and{}>;
  table[Op::BooleanOr] = &OnBinaryBool<std::logical_or{}>;
  table[Op::GreaterThan] = &OnBinaryInt<std::greater{}>;
  table[Op::GreaterThanOrEqual] = &OnBinaryInt<std::greater_equal{}>;
  table[Op::LessThan] = &OnBinaryInt<std::less{}>;
  table[Op::LessThanOrEqual] = &OnBinaryInt<std::less_equal{}>;
  table[Op::Min] = &OnBinaryInt<kMin>;
  table[Op::Max] = &OnBinaryInt<kMax>;
  table[Op::NumEqual] = &OnBinaryInt<std::equal_to{}>;
  table[Op::NumEqualVerify] = &OnNumEqualVerify;
  table[Op::NumNotEqual] = &OnBinaryInt<std::not_equal_to{}>;
  table[Op::Subtract] = &OnBinaryInt<std::minus{}>;
}

}  // namespace hornet::protocol::script::runtime

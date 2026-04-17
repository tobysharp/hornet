// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "hornetlib/protocol/script/lang/minimal.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/decode.h"
#include "hornetlib/protocol/script/runtime/throw.h"
#include "hornetlib/util/subarray.h"

namespace hornet::protocol::script::runtime {

class Stack {
 public:
  Stack() {
    data_.reserve(kMaxItemSize * kMaxItems);
    items_.reserve(kMaxItems);
  }

  bool Empty() const { return items_.empty(); }

  int Size() const { return std::ssize(items_); }

  void Clear() {
    items_.clear();
    data_.clear();
  }

  Stack& Push(lang::Bytes bytes) {
    if (std::ssize(items_) >= kMaxItems)
      Throw(lang::Error::StackOverflow, "Stack overflow: exceeded the limit of ", kMaxItems, " items.");
    if (std::ssize(bytes) > kMaxItemSize)
      Throw(lang::Error::StackItemOverflow, "Stack item overflow: ", bytes.size(), " bytes exceeded ", kMaxItemSize,
            " byte size limit.");
    items_.emplace_back(Item{int(std::ssize(data_)), int16_t(std::ssize(bytes))});
    data_.insert(data_.end(), bytes.begin(), bytes.end());
    return *this;
  }

  Stack& Push(uint8_t byte);  // Avoid the confusion of whether to push as a raw byte or an integer.

  Stack& Push(bool flag) { return Push(flag ? 1 : 0); }

  template <std::integral T>
  Stack& Push(T x) {
    return Push(lang::EncodeMinimalInt(x));
  }

  Stack& Pop(int count) {
    if (Size() < count) Throw(lang::Error::StackUnderflow, "Pop() of empty stack.");
    items_.resize(items_.size() - count);
    data_.resize(items_.empty() ? 0 : items_.back().EndIndex());
    return *this;
  }

  lang::Bytes Top() const {
    if (Empty()) Throw(lang::Error::StackUnderflow, "Top() of empty stack.");
    return items_.back().Span(data_);
  }

  lang::Bytes Pop() {
    const auto rv = Top();
    Pop(1);
    return rv;
  }

  // Interpret the top-of-stack as a Boolean. Throws if stack is empty.
  bool TopAsBool() const {
    const auto top = Top();
    for (int i = 0; i < std::ssize(top); ++i)
      if (top[i] != 0) return i < std::ssize(top) - 1 || top[i] != 0x80;
    return false;
  }

  // Interpret the stack item at the given position as a 32-bit integer.
  int32_t Int32(int position = 0, bool require_minimal = true) const {
    if (position >= Size()) Throw(lang::Error::StackUnderflow, "Int32 of invalid stack position.");
    return DecodeInt32(Peek(position), require_minimal);
  }

  // Retrieve the stack item at the given position.
  std::span<const uint8_t> Peek(int position) const {
    if (position >= Size()) Throw(lang::Error::StackUnderflow, "Accessed an invalid stack position.");
    int index = std::ssize(items_) - 1 - position;
    return items_[index].Span(data_);
  }

  template <typename Fn>
  void Call(Fn&& fn);

 protected:
  static constexpr int kMaxItems = 1'000;
  static constexpr int kMaxItemSize = 520;
  using Item = util::SubArray<uint8_t, int16_t>;
  std::vector<Item> items_;
  std::vector<uint8_t> data_;
};

namespace detail {
template <typename T>
struct function_traits : function_traits<decltype(&T::operator())> {};

template <typename ReturnType, typename... Args>
struct function_traits<ReturnType (*)(Args...)> {
  using args_tuple = std::tuple<Args...>;
  using result_type = ReturnType;
};

template <typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType (ClassType::*)(Args...) const> {
  using args_tuple = std::tuple<Args...>;
  using result_type = ReturnType;
};
}  // namespace detail

template <typename Fn>
inline void Stack::Call(Fn&& fn) {
  using FnType = std::decay_t<Fn>;
  using Traits = detail::function_traits<FnType>;
  using ArgsTuple = typename Traits::args_tuple;
  constexpr size_t N = std::tuple_size_v<ArgsTuple>;
  using Result = typename Traits::result_type;

  auto args = [&]<size_t... I>(std::index_sequence<I...>) {
    return std::make_tuple(Peek(N - 1 - I)...);
  }(std::make_index_sequence<N>{});

  if constexpr (std::is_void_v<Result>) {
    std::apply(std::forward<Fn>(fn), args);
    Pop(N);
  } else {
    auto result = std::apply(std::forward<Fn>(fn), args);
    Pop(N);
    Push(result);
  }
}

}  // namespace hornet::protocol::script::runtime

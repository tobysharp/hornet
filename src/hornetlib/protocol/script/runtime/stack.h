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
  static constexpr int kMaxItems = 1'000;

  Stack() = default;

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

  Stack& Pop(int count = 1) {
    if (Size() < count) Throw(lang::Error::StackUnderflow, "Pop() of empty stack.");
    items_.resize(items_.size() - count);
    data_.resize(items_.empty() ? 0 : items_.back().EndIndex());
    return *this;
  }

  lang::Bytes Top() const {
    if (Empty()) Throw(lang::Error::StackUnderflow, "Top() of empty stack.");
    return items_.back().Span(data_);
  }

  // Interpret the top-of-stack as a Boolean. Throws if stack is empty.
  bool TopAsBool() const { return lang::AsBool(Top()); }

  // Interpret the stack item at the given position as a 32-bit integer.
  int32_t Int32(int position = 0, bool require_minimal = true) const {
    if (position < 0 || position >= Size()) Throw(lang::Error::StackUnderflow, "Int32 of invalid stack position.");
    return DecodeInt32(Peek(position), require_minimal);
  }

  // Retrieve the stack item at the given position.
  std::span<const uint8_t> Peek(int position) const {
    if (position < 0 || position >= Size()) Throw(lang::Error::StackUnderflow, "Accessed an invalid stack position.");
    return items_.rbegin()[position].Span(data_);
  }

  template <typename Fn>
  void Call(Fn&& fn);

  template <int kPopCount, auto kIndices>
  void ModifyTop();
  template <auto kIndices>
  void CopyToTop();
  void MoveToTop(int position);
  void CopyToTop(int position) { Push(Peek(position)); }

 protected:
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

template <auto kIndices>
inline void Stack::CopyToTop() {
  constexpr int kAppendCount = std::ssize(kIndices);
  for (int i = 0; i < kAppendCount; ++i) Push(Peek(kIndices[kAppendCount - 1 - i] + i));
}

template <int kPopCount, auto kIndices>
inline void Stack::ModifyTop() {
  using T = std::remove_cvref_t<decltype(kIndices)>;
  static_assert(std::is_same_v<T, std::array<int, std::tuple_size_v<T>>>);
  static_assert([] {
    for (int index : kIndices)
      if (index < 0 || index >= kPopCount) return false;
    return true;
  }());
  constexpr int kPushCount = std::ssize(kIndices);
  constexpr auto kRequired = [] {
    std::array<bool, kPopCount> required = {};
    for (int index : kIndices) required[index] = true;
    return required;
  }();
  std::array<Item, kPopCount> tmp_items;
  std::vector<uint8_t> tmp_data;

  int size_bytes = 0;
  for (int i = 0; i < kPopCount; ++i)
    if (kRequired[i]) size_bytes += std::ssize(Peek(i));
  tmp_data.reserve(size_bytes);

  for (int i = 0; i < kPopCount; ++i) {
    if (!kRequired[i]) continue;
    const auto peek = Peek(i);
    tmp_items[i] = {static_cast<int>(std::ssize(tmp_data)), static_cast<int16_t>(std::ssize(peek))};
    tmp_data.insert_range(tmp_data.end(), peek);
  }

  Pop(kPopCount);

  for (int i = kPushCount - 1; i >= 0; --i) Push(tmp_items[kIndices[i]].Span(tmp_data));
}

inline void Stack::MoveToTop(int position) {
  if (position < 0 || position >= Size()) Throw(lang::Error::StackUnderflow);

  const int source_index = Size() - 1 - position;
  const auto src = items_[source_index];
  const int item_size = src.Size();
  const int data_size = std::ssize(data_);

  std::rotate(data_.begin() + src.StartIndex(), data_.begin() + src.EndIndex(), data_.end());
  std::rotate(items_.begin() + source_index, items_.begin() + source_index + 1, items_.end());

  for (int i = source_index; i < Size() - 1; ++i) items_[i].Offset(-item_size);

  items_.back() = {data_size - item_size, static_cast<int16_t>(item_size)};
}

class ConditionStack {
 public:
  operator bool() const { return first_false_ < 0; }
  bool Empty() const { return size_ < 1; }
  void Toggle() {
    if (size_ < 1) Throw(lang::Error::UnbalancedCondition);
    if (first_false_ < 0) first_false_ = size_ - 1;
    else if (first_false_ == size_ - 1) first_false_ = -1;
  }
  void Push(bool value) {
    ++size_;
    if (first_false_ < 0 && !value) first_false_ = size_ - 1;
  }
  void Pop() {
    if (size_ < 1) Throw(lang::Error::UnbalancedCondition);
    --size_;
    if (first_false_ >= size_) first_false_ = -1;
  }

 private:
  int size_ = 0;
  int first_false_ = -1;
};

}  // namespace hornet::protocol::script::runtime

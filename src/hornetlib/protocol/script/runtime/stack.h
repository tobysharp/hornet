#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/throw.h"
#include "hornetlib/util/subarray.h"

namespace hornet::protocol::script::runtime {

class Stack {
 public:
  Stack() {
    data_.reserve(kMaxItemSize * kMaxItems);
    items_.reserve(kMaxItems);
  }
  bool Empty() const {
    return items_.empty();
  }
  int Size() const {
    return std::ssize(items_);
  }
  void Clear() {
    items_.clear();
    data_.clear();
  }
  void Push(lang::Bytes bytes) {
    if (std::ssize(items_) >= kMaxItems)
      Throw(lang::Error::StackOverflow, "Stack overflow: exceeded the limit of ", kMaxItems,
            " items.");
    if (std::ssize(bytes) > kMaxItemSize)
      Throw(lang::Error::StackItemOverflow, "Stack item overflow: ", bytes.size(),
            " bytes exceeded ", kMaxItemSize, " byte size limit.");
    items_.emplace_back(Item{int(std::ssize(data_)), int16_t(std::ssize(bytes))});
    data_.insert(data_.end(), bytes.begin(), bytes.end());
  }
  void Push(uint8_t byte) {
    Push({&byte, 1});
  }
  void Pop() {
    if (Empty()) Throw(lang::Error::StackUnderflow, "Pop() of empty stack.");
    items_.pop_back();
    data_.resize(items_.empty() ? 0 : items_.back().EndIndex());
  }
  lang::Bytes Top() const {
    if (Empty()) Throw(lang::Error::StackUnderflow, "Top() of empty stack.");
    return items_.back().Span(data_);
  }
  // Interpret the top-of-stack as a Boolean. Throws if stack is empty.
  bool TopAsBool() const {
    const auto top = Top();
    for (int i = 0; i < std::ssize(top); ++i)
      if (top[i] != 0) return i < std::ssize(top) - 1 || top[i] != 0x80;
    return false;
  }

 protected:
  static constexpr int kMaxItems = 1'000;
  static constexpr int kMaxItemSize = 520;
  using Item = util::SubArray<uint8_t, int16_t>;
  std::vector<Item> items_;
  std::vector<uint8_t> data_;
};

}  // namespace hornet::protocol::script::runtime

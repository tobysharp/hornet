#pragma once

#include <optional>
#include <span>

#include "hornetlib/protocol/script/instruction.h"
#include "hornetlib/protocol/script/op.h"
#include "hornetlib/protocol/script/parser.h"
#include "hornetlib/protocol/script/runtime/exception.h"
#include "hornetlib/protocol/script/runtime/int_decoder.h"
#include "hornetlib/util/subarray.h"

namespace hornet::protocol::script {

class Processor {
 public:
  explicit Processor(std::span<const uint8_t> script) : parser_(script) {}

  // Step execution forward by one instruction.
  void Step() {
    if (const auto instruction = parser_.Next()) Execute(*instruction);
  }

  // Run the script to the end and return its Boolean result.
  bool Run() {
    try {
      while (const auto instruction = parser_.Next()) Execute(*instruction);
      return stack_.Empty() ? false : stack_.TopAsBool();
    }
    catch (const runtime::Exception& e) {
      // TODO: Log execution and error state.
      return false;
    }
  }

  // Try to interpret the top-of-stack as a 32-bit signed integer, if valid.
  std::optional<int32_t> TryPeekInt() const {
    if (stack_.Empty()) return std::nullopt;
    try {
      return decoder_.Decode4(stack_.Top());
    }
    catch (const runtime::Exception&) {
      return std::nullopt;
    }
  }

  const Parser& Parser() const {
    return parser_;
  }

 private:
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
    void PushEmpty() {
      items_.emplace_back(Item{});
    }
    void PushData(std::span<const uint8_t> bytes) {
      if (std::ssize(bytes) > kMaxItemSize) runtime::Throw("Push: Item too large.");
      if (std::ssize(items_) >= kMaxItems) runtime::Throw("Push: Too many items.");
      items_.emplace_back(Item{int(std::ssize(data_)), int16_t(std::ssize(bytes))});
      data_.insert(data_.end(), bytes.begin(), bytes.end());
    }
    void PushByte(uint8_t byte) {
      PushData({&byte, 1});
    }
    void PushImmediate(int8_t value) {
      Assert(IsImmediate(value) && value != 0);
      // PushData(EncodeMinimalInt(value));
      PushByte(value == -1 ? 0x81 : uint8_t(value));
    }
    std::span<const uint8_t> Top() const {
      if (Empty()) runtime::Throw("It is invalid to call Top on an empty stack.");
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

  void Execute(const Instruction& instruction) {
    if (IsPush(instruction.opcode)) {
      if (instruction.opcode == Op::PushEmpty)
        stack_.PushEmpty();
      else if (IsImmediate(instruction.opcode))
        stack_.PushImmediate(OpToImmediate(instruction.opcode));
      else {
        // TODO: Optionally verify that the data we're told to push couldn't have been pushed with a
        // simpler opcode.
        stack_.PushData(instruction.data);
      }
    }
  }

  script::Parser parser_;
  Stack stack_;
  runtime::MinimalIntDecoder decoder_;
};

}  // namespace hornet::protocol::script

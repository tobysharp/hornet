#pragma once

#include <cstdint>
#include <ranges>
#include <span>
#include <vector>

#include "hornetlib/encoding/reader.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/protocol/hash.h"

namespace hornet::protocol {

template <typename T, std::integral Count = int>
class SubArray {
 public:
  SubArray() : start_(0), count_(0) {}
  SubArray(int start, Count count) : start_(start), count_(count) {}
  SubArray(const SubArray&) = default;
  SubArray& operator=(const SubArray&) = default;

  int StartIndex() const {
    return start_;
  }
  Count Size() const {
    return count_;
  }

  bool IsEmpty() const {
    return count_ <= 0;
  }

  std::span<T> Span(std::span<T> data) {
    return data.subspan(start_, count_);
  }

  std::span<const T> Span(std::span<const T> data) const {
    return data.subspan(start_, count_);
  }

 private:
  int start_;
  Count count_;
};

struct OutPoint {
  Hash hash;
  uint32_t index;

  void Deserialize(encoding::Reader& reader) {
    reader.ReadBytes(hash);
    reader.ReadLE4(index);
  }
};

using ScriptArray = SubArray<uint8_t, uint16_t>;
using Component = ScriptArray;
using Witness = SubArray<Component>;

struct Input;
struct Output;

struct TransactionData {
  std::vector<Input> inputs;
  std::vector<Output> outputs;
  std::vector<Witness> witnesses;
  std::vector<Component> components;
  std::vector<uint8_t> scripts;

  SubArray<Input> AddInputs(int size) {
    return AddToVector(inputs, size);
  }
  SubArray<Output> AddOutputs(int size) {
    return AddToVector(outputs, size);
  }
  SubArray<Witness> AddWitnesses(int size) {
    return AddToVector(witnesses, size);
  }
  SubArray<Component> AddComponents(int size) {
    return AddToVector(components, size);
  }
  ScriptArray AddScriptBytes(uint16_t size) {
    return AddToVector(scripts, size);
  }

 private:
  template <typename T, std::integral Count>
  static SubArray<T, Count> AddToVector(std::vector<T>& vec, Count size) {
    vec.resize(vec.size() + size);
    return {static_cast<int>(std::ssize(vec) - size), size};
  }
};

struct Input {
  OutPoint previous_output;
  ScriptArray signature_script;
  uint32_t sequence;

  void Deserialize(encoding::Reader& reader, TransactionData& data) {
    previous_output.Deserialize(reader);
    signature_script = data.AddScriptBytes(reader.ReadVarInt<uint16_t>());
    reader.ReadBytes(util::AsByteSpan(signature_script.Span(data.scripts)));
    reader.ReadLE4(sequence);
  }
};

struct Output {
  int64_t value;
  ScriptArray pk_script;

  void Deserialize(encoding::Reader& reader, TransactionData& data) {
    reader.ReadLE8(value);
    pk_script = data.AddScriptBytes(reader.ReadVarInt<uint16_t>());
    reader.ReadBytes(util::AsByteSpan(pk_script.Span(data.scripts)));
  }
};

class TransactionDetail {
 public:
  std::span<const Input> Inputs(const TransactionData& data) const {
    return inputs_.Span(data.inputs);
  }
  std::span<Input> Inputs(TransactionData& data) {
    return inputs_.Span(data.inputs);
  }

  bool IsWitness() const {
    return !witnesses_.IsEmpty();
  }

  void Serialize(encoding::Writer&, const TransactionData&) const {
    // TODO
  }

  void Deserialize(encoding::Reader& reader, TransactionData& data) {
    // Version
    reader.ReadLE4(version_);

    // Optional witness flag
    bool witness = false;
    uint8_t byte = reader.ReadByte();
    if (byte == 0) {
      // Zero implies two-byte witness flag
      byte = reader.ReadByte();  // Second byte should be 0x01
      if (byte != 1) util::ThrowRuntimeError("Unexpected witness flag byte.");
      witness = true;
    } else {
      // Rewind the byte we peeked
      reader.Seek(reader.GetPos() - 1);
    }

    // Inputs
    inputs_ = data.AddInputs(reader.ReadVarInt());
    for (Input& input : inputs_.Span(data.inputs)) input.Deserialize(reader, data);

    // Outputs
    outputs_ = data.AddOutputs(reader.ReadVarInt());
    for (Output& output : outputs_.Span(data.outputs)) output.Deserialize(reader, data);

    // Witnesses
    if (witness) {
      witnesses_ = data.AddWitnesses(inputs_.Size());
      for (Witness& witness : witnesses_.Span(data.witnesses)) {
        witness = data.AddComponents(reader.ReadVarInt<int>());
        for (ScriptArray& component : witness.Span(data.components)) {
          component = data.AddScriptBytes(reader.ReadVarInt<uint16_t>());
          reader.ReadBytes(util::AsByteSpan(component.Span(data.scripts)));
        }
      }
    }
  }

 private:
  uint32_t version_;
  SubArray<Input> inputs_;
  SubArray<Output> outputs_;
  SubArray<Witness> witnesses_;
  [[maybe_unused]] uint32_t lock_time_;
};

template <typename Data, typename Detail>
class TransactionViewT {
 public:
  TransactionViewT(Data& data, Detail& detail) : data_(data), detail_(detail) {}
   
  uint32_t Version() const {
    return detail_.version_;
  }
  std::span<const Input> Inputs() const {
    return detail_.Inputs(data_);
  }
  std::span<const Output> Ouptuts() const {
    return detail_.Outputs(data_);
  }
  std::span<const Witness> Witnesses() const {
    return detail_.Witnesses(data);
  }
  uint32_t LockTime() const {
    return detail_.lock_time;
  }

  // This non-const method is chosen when the view itself is passed around.
  // The returned Input objects will be const if Data and Detail are const.
  // Hence the return type is auto.
  auto Inputs() {
    return detail_.Inputs(data_);
  }

 protected:
  Data& data_;
  Detail& detail_;
};

using TransactionView = TransactionViewT<TransactionData, TransactionDetail>;
using TransactionConstView = TransactionViewT<const TransactionData, const TransactionDetail>;

// Standalone transaction class
class Transaction : public TransactionView {
 public:
  Transaction() : TransactionView(data_, detail_) {}

  void Deserialize(encoding::Reader& reader) {
    detail_.Deserialize(reader, data_);
  }

 private:
  TransactionData data_;
  TransactionDetail detail_;
};

inline void foo() {
  Transaction tx1;
  const Transaction tx2;
  [[maybe_unused]] std::span<Input> in = tx1.Inputs();
  [[maybe_unused]] std::span<const Input> in2 = tx2.Inputs();

  // Mimics
  //   TransactionConstView Block::GetTransaction(int index) const;
  const TransactionData data;
  const TransactionDetail detail = {};
  TransactionConstView v{data, detail};

  [[maybe_unused]] std::span<const Input> in3 = v.Inputs();
}

}  // namespace hornet::protocol

#pragma once

#include <cstdint>
#include <ranges>
#include <span>
#include <vector>

#include "hornetlib/encoding/reader.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/util/subarray.h"

namespace hornet::protocol {

struct OutPoint {
  Hash hash;
  uint32_t index;

  void Deserialize(encoding::Reader& reader) {
    reader.ReadBytes(hash);
    reader.ReadLE4(index);
  }
};

using ScriptArray = util::SubArray<uint8_t, uint16_t>;
using Component = ScriptArray;
using Witness = util::SubArray<Component>;

struct Input;
struct Output;

// The TransactionData struct stores all the variable-sized array elements for one or more
// transactions. The purpose of separating out the data in this way is to allow for flat allocation
// of all fields across a block of transactions, completely eliminating jagged arrays and their heap
// fragmentation, while also improving cache coherence.
struct TransactionData {
  std::vector<Input> inputs;
  std::vector<Output> outputs;
  std::vector<Witness> witnesses;
  std::vector<Component> components;
  std::vector<uint8_t> scripts;

  util::SubArray<Input> AddInputs(int size) {
    return AddToVector(inputs, size);
  }
  util::SubArray<Output> AddOutputs(int size) {
    return AddToVector(outputs, size);
  }
  util::SubArray<Witness> AddWitnesses(int size) {
    return AddToVector(witnesses, size);
  }
  util::SubArray<Component> AddComponents(int size) {
    return AddToVector(components, size);
  }
  ScriptArray AddScriptBytes(uint16_t size) {
    return AddToVector(scripts, size);
  }

 private:
  template <typename T, std::integral Count>
  static util::SubArray<T, Count> AddToVector(std::vector<T>& vec, Count size) {
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

// The TransactionDetail struct holds the data fields of a transaction, and the
// metadata needed for its variable-length array fields. The actual data for those
// arrays is held in TransactionData.
struct TransactionDetail {
  uint32_t version;
  util::SubArray<Input> inputs;
  util::SubArray<Output> outputs;
  util::SubArray<Witness> witnesses;
  uint32_t lock_time;

  bool IsWitness() const {
    return !witnesses.IsEmpty();
  }

  void Serialize(encoding::Writer&, const TransactionData&) const {
    // TODO
  }

  void Deserialize(encoding::Reader& reader, TransactionData& data) {
    // Version
    reader.ReadLE4(version);

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
    inputs = data.AddInputs(reader.ReadVarInt());
    for (Input& input : inputs.Span(data.inputs)) input.Deserialize(reader, data);

    // Outputs
    outputs = data.AddOutputs(reader.ReadVarInt());
    for (Output& output : outputs.Span(data.outputs)) output.Deserialize(reader, data);

    // Witnesses
    if (witness) {
      witnesses = data.AddWitnesses(inputs.Size());
      for (Witness& witness : witnesses.Span(data.witnesses)) {
        witness = data.AddComponents(reader.ReadVarInt<int>());
        for (ScriptArray& component : witness.Span(data.components)) {
          component = data.AddScriptBytes(reader.ReadVarInt<uint16_t>());
          reader.ReadBytes(util::AsByteSpan(component.Span(data.scripts)));
        }
      }
    }
  }
};

// The TransactionViewT class represents the join of data and metadata stored in
// TransactionData and TransactionDetail respectively. This allows for semantically
// meaningful operations on transaction fields and sub-fields, and hides the implementation
// details of flat allocation via SubArray.
template <typename Data, typename Detail>
class TransactionViewT {
 public:
  TransactionViewT(Data& data, Detail& detail) : data_(data), detail_(detail) {}

  // The following const member methods are chosen by the compiler in the case where
  // the TransactionViewT object is const, e.g. the method is called on a const object that
  // derives from TransactionViewT.
  uint32_t Version() const {
    return detail_.version;
  }
  bool IsWitness() const {
    return detail_.IsWitness();
  }
  const Input& Input(int index) const {
    return detail_.inputs.Span(data_.inputs)[index];
  }
  const Output& Output(int index) const {
    return detail_.outputs.Span(data_.outputs)[index];
  }
  std::span<const uint8_t> SignatureScript(int input) const {
    return Input(input).signature_script.Span(data_.scripts);
  }
  std::span<const uint8_t> PkScript(int output) const {
    return Output(output).pk_script.Span(data_.scripts);
  }
  std::span<const uint8_t> WitnessScript(int input, int component) const {
    const Witness& witness = detail_.witnesses.Span(data_.witnesses)[input];
    const Component& subarray = witness.Span(data_.components)[component];
    return subarray.Span(data_.scripts);
  }
  uint32_t LockTime() const {
    return detail_.lock_time;
  }

  // The following non-const member methods are chosen by the compiler in the case where
  // the TransactionViewT object is non-const. In this case, the constness of the return value
  // depends on the constness of the templated types (Data, Detail).
  auto& Version() {
    return detail_.version;
  }
  auto& Input(int index) {
    return detail_.inputs.Span(data_.inputs)[index];
  }
  auto& Output(int index) {
    return detail_.outputs.Span(data_.outputs)[index];
  }
  auto SignatureScript(int input) {
    return Input(input).signature_script.Span(data_.scripts);
  }
  auto PkScript(int output) {
    return Output(output).pk_script.Span(data_.scripts);
  }
  auto WitnessScript(int input, int component) {
    const Witness& witness = detail_.witnesses.Span(data_.witnesses)[input];
    const Component& subarray = witness.Span(data_.components)[component];
    return subarray.Span(data_.scripts);
  }
  auto& LockTime() {
    return detail_.lock_time;
  }

 protected:
  Data& data_;
  Detail& detail_;
};

using TransactionView = TransactionViewT<TransactionData, TransactionDetail>;
using TransactionConstView = TransactionViewT<const TransactionData, const TransactionDetail>;

// Standalone transaction class, inheriting TransactionView behavior.
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

}  // namespace hornet::protocol

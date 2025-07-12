#pragma once

#include <cstdint>
#include <ranges>
#include <span>
#include <vector>

#include "hornetlib/encoding/reader.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/util/log.h"
#include "hornetlib/util/subarray.h"

namespace hornet::protocol {

struct OutPoint {
  Hash hash = {};
  uint32_t index = 0;

  void Serialize(encoding::Writer& writer) const {
    writer.WriteBytes(hash);
    writer.WriteLE4(index);
  }

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

  void ResizeInputs(util::SubArray<Input>& subarray, int size) {
    subarray = ResizeVector(inputs, subarray, size);
  }
  void ResizeOutputs(util::SubArray<Output>& subarray, int size) {
    subarray = ResizeVector(outputs, subarray, size);
  }
  void ResizeWitnesses(util::SubArray<Witness>& subarray, int size) {
    subarray = ResizeVector(witnesses, subarray, size);
  }
  void ResizeComponents(util::SubArray<Component>& subarray, int size) {
    subarray = ResizeVector(components, subarray, size);
  }
  void ResizeScriptBytes(ScriptArray& subarray, uint16_t size) {
    subarray = ResizeVector(scripts, subarray, size);
  }
  int SizeBytes() const;
 private:
  template <typename T, std::integral Count>
  static util::SubArray<T, Count> ResizeVector(std::vector<T>& vec, const util::SubArray<T, Count>& subarray, Count size) {
    const int length = std::ssize(vec);
    const int start = subarray.StartIndex();
    const int end = subarray.EndIndex();
    Assert(end <= length);

    if (end == length) {
      // Rewind if the old subarray was the tail
      vec.resize(start + size);
      return {start, size};
    } else if (start + size <= end) {
      // Reuse existing slice (may leave garbage past end)
      return {start, size};
    } else {
      if (start < end) {
        // Can't recover space from previous allocation -- burn and log.
        LogWarn() << "Overwriting transaction data cost " << (end - start) << " bytes of memory.";
      }
      vec.resize(length + size);
      return {length, size};
    }
  }
};

struct Input {
  OutPoint previous_output;
  ScriptArray signature_script;
  uint32_t sequence = 0;

  void Serialize(encoding::Writer& writer, const TransactionData& data) const {
    previous_output.Serialize(writer);
    writer.WriteVarInt(signature_script.Size());
    writer.WriteBytes(signature_script.Span(data.scripts));
    writer.WriteLE4(sequence);
  }

  void Deserialize(encoding::Reader& reader, TransactionData& data) {
    previous_output.Deserialize(reader);
    data.ResizeScriptBytes(signature_script, reader.ReadVarInt<uint16_t>());
    reader.ReadBytes(signature_script.Span(data.scripts));
    reader.ReadLE4(sequence);
  }
};

struct Output {
  int64_t value = 0;
  ScriptArray pk_script;

  void Serialize(encoding::Writer& writer, const TransactionData& data) const {
    writer.WriteLE8(value);
    writer.WriteVarInt(pk_script.Size());
    writer.WriteBytes(pk_script.Span(data.scripts));
  }

  void Deserialize(encoding::Reader& reader, TransactionData& data) {
    reader.ReadLE8(value);
    data.ResizeScriptBytes(pk_script, reader.ReadVarInt<uint16_t>());
    reader.ReadBytes(pk_script.Span(data.scripts));
  }
};

inline int TransactionData::SizeBytes() const {
  size_t size = sizeof(*this);
  size += inputs.capacity() * sizeof(Input);
  size += outputs.capacity() * sizeof(Output);
  size += witnesses.capacity() * sizeof(Witness);
  size += components.capacity() * sizeof(Component);
  size += scripts.capacity() * sizeof(uint8_t);
  return static_cast<int>(size);
}
 
// The TransactionDetail struct holds the data fields of a transaction, and the
// metadata needed for its variable-length array fields. The actual data for those
// arrays is held in TransactionData.
struct TransactionDetail {
  uint32_t version = 0;
  util::SubArray<Input> inputs;
  util::SubArray<Output> outputs;
  util::SubArray<Witness> witnesses;
  uint32_t lock_time = 0;

  bool IsWitness() const {
    return !witnesses.IsEmpty();
  }

  void Serialize(encoding::Writer& writer, const TransactionData& data) const {
    // Version
    writer.WriteLE4(version);

    // Optional witness flag
    if (IsWitness()) writer.WriteLE2(0x0100);

    // Inputs
    writer.WriteVarInt(inputs.Size());
    for (const Input& input : inputs.Span(data.inputs)) 
      input.Serialize(writer, data);

    // Outputs
    writer.WriteVarInt(outputs.Size());
    for (const Output& output : outputs.Span(data.outputs)) 
      output.Serialize(writer, data);

    // Witnesses
    if (IsWitness()) {
      for (const Witness& witness : witnesses.Span(data.witnesses)) {
        writer.WriteVarInt(witness.Size());
        for (const ScriptArray& component : witness.Span(data.components)) {
          writer.WriteVarInt(component.Size());
          writer.WriteBytes(component.Span(data.scripts));
        }
      }
    }

    // Lock time
    writer.WriteLE4(lock_time);
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
    data.ResizeInputs(inputs, reader.ReadVarInt());
    for (Input& input : inputs.Span(data.inputs)) input.Deserialize(reader, data);

    // Outputs
    data.ResizeOutputs(outputs, reader.ReadVarInt());
    for (Output& output : outputs.Span(data.outputs)) output.Deserialize(reader, data);

    // Witnesses
    if (witness) {
      data.ResizeWitnesses(witnesses, inputs.Size());
      for (Witness& witness : witnesses.Span(data.witnesses)) {
        data.ResizeComponents(witness, reader.ReadVarInt<int>());
        for (ScriptArray& component : witness.Span(data.components)) {
          data.ResizeScriptBytes(component, reader.ReadVarInt<uint16_t>());
          reader.ReadBytes(component.Span(data.scripts));
        }
      }
    }

    // Lock time
    reader.ReadLE4(lock_time);
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

  int InputCount() const {
    return detail_.inputs.Size();
  }
  int OutputCount() const {
    return detail_.outputs.Size();
  }
  int WitnessCount() const {
    return detail_.witnesses.Size();
  }

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
  const Witness& Witness(int input) const {
    return detail_.witnesses.Span(data_.witnesses)[input];
  }
  const Component& Component(int input, int component) const {
    return Witness(input).Span(data_.components)[component];
  }
  std::span<const uint8_t> SignatureScript(int input) const {
    return Input(input).signature_script.Span(data_.scripts);
  }
  std::span<const uint8_t> PkScript(int output) const {
    return Output(output).pk_script.Span(data_.scripts);
  }
  std::span<const uint8_t> WitnessScript(int input, int component) const {
    return Component(input, component).Span(data_.scripts);
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
  auto& Witness(int input) {
    return detail_.witnesses.Span(data_.witnesses)[input];
  }
  auto& Component(int input, int component) {
    return Witness(input).Span(data_.components)[component];
  }
  auto SignatureScript(int input) {
    return Input(input).signature_script.Span(data_.scripts);
  }
  auto PkScript(int output) {
    return Output(output).pk_script.Span(data_.scripts);
  }
  auto WitnessScript(int input, int component) {
    return Component(input, component).Span(data_.scripts);
  }
  auto& LockTime() {
    return detail_.lock_time;
  }

  // The following methods are only valid on mutable views, and will cause compile errors if
  // called on immutable views, i.e. where detail_ and data_ are const.
  void SetVersion(int version) {
    detail_.version = version;
  }
  void ResizeInputs(int inputs) {
    data_.ResizeInputs(detail_.inputs, inputs);
  }
  void ResizeOutputs(int outputs) {
    data_.ResizeOutputs(detail_.outputs, outputs);
  }
  void ResizeWitnesses(int witnesses) {
    data_.ResizeWitnesses(detail_.witnesses, witnesses);
  }
  void ResizeComponents(int input, int components) {
    data_.ResizeComponents(Witness(input), components);
  }
  void SetSignatureScript(int input, std::span<const uint8_t> script) {
    SetScript(Input(input).signature_script, script);
  }
  void SetPkScript(int output, std::span<const uint8_t> script) {
    SetScript(Output(output).pk_script, script);
  }
  void SetWitnessScript(int input, int component, std::span<const uint8_t> script) {
    SetScript(Component(input, component), script);
  }
  void SetLockTime(uint32_t lock_time) {
    detail_.lock_time = lock_time;
  }

  template <typename Data2, typename Detail2>
  void CopyFrom(const TransactionViewT<Data2, Detail2>& rhs) {
    SetVersion(rhs.Version());
    ResizeInputs(rhs.InputCount());
    ResizeOutputs(rhs.OutputCount());
    ResizeWitnesses(rhs.WitnessCount());
    SetLockTime(rhs.LockTime());

    for (int i = 0; i < InputCount(); ++i)
    {
      Input(i).previous_output = rhs.Input(i).previous_output;
      Input(i).sequence = rhs.Input(i).sequence;
      SetSignatureScript(i, rhs.SignatureScript(i));
    }
    for (int i = 0; i < OutputCount(); ++i)
    {
      Output(i).value = rhs.Output(i).value;
      SetPkScript(i, rhs.PkScript(i));
    }
    for (int i = 0; i < rhs.WitnessCount(); ++i) {
      ResizeComponents(i, rhs.Witness(i).Size());
      for (int j = 0; j < Witness(i).Size(); ++j)
        SetWitnessScript(i, j, rhs.WitnessScript(i, j));
    }
  }

 protected:
  void SetScript(ScriptArray& script_array, std::span<const uint8_t> script) {
    data_.ResizeScriptBytes(script_array, static_cast<uint16_t>(script.size()));
    std::copy(script.begin(), script.end(), script_array.Span(data_.scripts).begin());
  }

  Data& data_;
  Detail& detail_;
};

// Define mutable and immutable transaction views.
using TransactionView = TransactionViewT<TransactionData, TransactionDetail>;
using TransactionConstView = TransactionViewT<const TransactionData, const TransactionDetail>;

// Define a concept for transaction view.
template <typename T>
class IsTransactionViewConvertible {
  template <typename Data, typename Detail> static std::true_type Test(const TransactionViewT<Data, Detail>*);
  static std::false_type Test(...);
 public:
  static constexpr bool value = decltype(Test(std::declval<T*>()))::value;
};
template <typename T> concept TransactionViewType = IsTransactionViewConvertible<std::remove_cvref_t<T>>::value;

// Standalone transaction class, inheriting TransactionView behavior.
class Transaction : public TransactionView {
 public:
  Transaction() : TransactionView(data_, detail_) {}

  operator TransactionConstView() const {
    return {data_, detail_};
  }

  void Serialize(encoding::Writer& writer) const {
    detail_.Serialize(writer, data_);
  }

  void Deserialize(encoding::Reader& reader) {
    detail_.Deserialize(reader, data_);
  }

 private:
  TransactionData data_;
  TransactionDetail detail_;
};

}  // namespace hornet::protocol

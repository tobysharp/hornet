// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <cstdint>
#include <numeric>
#include <ranges>
#include <span>
#include <tuple>
#include <vector>

#include "hornetlib/encoding/reader.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/util/log.h"
#include "hornetlib/util/subarray.h"
#include "hornetlib/util/big_uint.h"

namespace hornet::protocol {

struct OutPoint {
  Hash hash = {};
  uint32_t index = 0;
  static constexpr uint32_t kNullIndex = std::numeric_limits<uint32_t>::max();

  std::strong_ordering operator <=>(const OutPoint& rhs) const = default;

  static OutPoint Null() { return {{}, kNullIndex}; }

  bool IsNull() const {
    return !hash && index == kNullIndex;
  }

  void Serialize(encoding::Writer& writer) const {
    writer.WriteBytes(hash);
    writer.WriteLE4(index);
  }

  void Deserialize(encoding::Reader& reader) {
    reader.ReadBytes(hash);
    reader.ReadLE4(index);
  }
};

using ScriptArray = util::SubArray<uint8_t>;
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
  void ResizeScriptBytes(ScriptArray& subarray, int size) {
    subarray = ResizeVector(scripts, subarray, size);
  }
  int SizeBytes() const;

  // Returns the size in bytes of the serialized witness data.
  int GetWitnessBytes() const {
    return witness_bytes_;
  }
  void AddWitnessBytes(int bytes) {
    witness_bytes_ += bytes;
  }

 private:
  int witness_bytes_ = 0;

  template <typename T, std::integral Count>
  static util::SubArray<T, Count> ResizeVector(std::vector<T>& vec,
                                               const util::SubArray<T, Count>& subarray,
                                               Count size) {
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
  ScriptArray signature_script = {};
  uint32_t sequence = 0;

  void Serialize(encoding::Writer& writer, const TransactionData& data) const {
    previous_output.Serialize(writer);
    writer.WriteVarInt(signature_script.Size());
    writer.WriteBytes(signature_script.Span(data.scripts));
    writer.WriteLE4(sequence);
  }

  void Deserialize(encoding::Reader& reader, TransactionData& data) {
    previous_output.Deserialize(reader);
    data.ResizeScriptBytes(signature_script, reader.ReadVarInt<int>());
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
    data.ResizeScriptBytes(pk_script, reader.ReadVarInt<int>());
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

struct TransactionDetail;
protocol::Hash ComputeTxid(const TransactionDetail& detail, const TransactionData& data, bool include_witness);

// The TransactionDetail struct holds the data fields of a transaction, and the
// metadata needed for its variable-length array fields. The actual data for those
// arrays is held in TransactionData.
struct TransactionDetail {
  uint32_t version = 0;
  util::SubArray<Input> inputs;
  util::SubArray<Output> outputs;
  util::SubArray<Witness> witnesses;
  uint32_t lock_time = 0;
  int no_witness_size_bytes = 0;
  mutable std::optional<protocol::Hash> txid;
  mutable std::optional<protocol::Hash> wtxid;

  bool IsWitness() const {
    return !witnesses.IsEmpty();
  }

  const protocol::Hash& GetHash(const TransactionData& data) const {
    if (!txid) {
      // Computes the txid, which is the double-SHA256 hash of the serialized transaction,
      // excluding any witness data from the serialization.
      txid = ComputeTxid(*this, data, false);
    }
    return *txid;
  }

  const protocol::Hash& GetWitnessHash(const TransactionData& data) const {
    if (!wtxid) {
      // Computes the wtxid, which is the double-SHA256 hash of the serialized transaction,
      // including any witness data in the serialization.
      wtxid = ComputeTxid(*this, data, true);
    }
    return *wtxid;
  }

  void Serialize(encoding::Writer& writer, const TransactionData& data, bool include_witness = true) const {
    if (inputs.Size() == 0)
      util::ThrowOutOfRange("Transaction has zero inputs and can't be serialized.");

    // Version
    writer.WriteLE4(version);

    // Optional witness flag
    if (IsWitness() && include_witness) writer.WriteLE2(0x0100);

    // Inputs
    writer.WriteVarInt(inputs.Size());
    for (const Input& input : inputs.Span(data.inputs)) input.Serialize(writer, data);

    // Outputs
    writer.WriteVarInt(outputs.Size());
    for (const Output& output : outputs.Span(data.outputs)) output.Serialize(writer, data);

    // Witnesses
    if (IsWitness() && include_witness) {
      for (const Witness& witness : witnesses.Span(data.witnesses)) {
        writer.WriteVarInt(witness.Size());
        for (const Component& component : witness.Span(data.components)) {
          writer.WriteVarInt(component.Size());
          writer.WriteBytes(component.Span(data.scripts));
        }
      }
    }

    // Lock time
    writer.WriteLE4(lock_time);
  }

  void Deserialize(encoding::Reader& reader, TransactionData& data) {
    int witness_size_bytes = 0;
    const auto start = reader.GetPos();
    // Version
    reader.ReadLE4(version);

    // Optional witness flag
    // TODO: Must pass a flag to TransactionDetail::Deserialize to say whether witness is allowed.
    // https://linear.app/hornet-node/issue/HOR-56/must-pass-a-flag-to-transactiondetaildeserialize-to-say-whether
    bool witness = false;
    uint8_t byte = reader.ReadByte();
    if (byte == 0) {
      // Zero implies two-byte witness flag
      byte = reader.ReadByte();  // Second byte should be 0x01
      if (byte != 1) util::ThrowRuntimeError("Unexpected witness flag byte.");
      witness = true;
      witness_size_bytes += 2;
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
      const auto witness_start = reader.GetPos();
      data.ResizeWitnesses(witnesses, inputs.Size());
      for (Witness& witness : witnesses.Span(data.witnesses)) {
        data.ResizeComponents(witness, reader.ReadVarInt<int>());
        for (Component& component : witness.Span(data.components)) {
          data.ResizeScriptBytes(component, reader.ReadVarInt<int>());
          reader.ReadBytes(component.Span(data.scripts));
        }
      }
      const int witness_bytes = reader.GetPos() - witness_start;
      witness_size_bytes += witness_bytes;
    }

    // Lock time
    reader.ReadLE4(lock_time);
    
    // Set number of serialized bytes, used during transaction validation.
    const int total_bytes = reader.GetPos() - start;
    no_witness_size_bytes = total_bytes - witness_size_bytes;
    data.AddWitnessBytes(witness_size_bytes);
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
  bool IsWitness() const {
    return detail_.IsWitness();
  }
  bool IsCoinBase() const {
    return InputCount() == 1 && Input(0).previous_output.IsNull();
  }
  int SerializedBytesNoWitness() const {
    return detail_.no_witness_size_bytes;
  }
  const protocol::Hash& GetHash() const {
    return detail_.GetHash(data_);
  }
  const protocol::Hash& GetWitnessHash() const {
    return detail_.GetWitnessHash(data_);
  }

  // The following const member methods are chosen by the compiler in the case where
  // the TransactionViewT object is const, e.g. the method is called on a const object that
  // derives from TransactionViewT.
  uint32_t Version() const {
    return detail_.version;
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
  std::span<const struct Input> Inputs() const {
    return detail_.inputs.Span(data_.inputs);
  }
  std::span<const struct Output> Outputs() const {
    return detail_.outputs.Span(data_.outputs);
  }
  auto SignatureScripts() const {
    return std::views::iota(0, InputCount()) | 
           std::views::transform([&] (const int i) { return SignatureScript(i); });
  }
  auto PkScripts() const {
    return std::views::iota(0, OutputCount()) | 
           std::views::transform([&] (const int i) { return PkScript(i); });
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
  auto Inputs() {
    return detail_.inputs.Span(data_.inputs);
  }
  auto Outputs() {
    return detail_.outputs.Span(data_.outputs);
  }
  auto SignatureScripts() {
    return std::views::iota(0, InputCount()) | 
           std::views::transform([&] (const int i) { return SignatureScript(i); });
  }
  auto PkScripts() {
    return std::views::iota(0, OutputCount()) | 
           std::views::transform([&] (const int i) { return PkScript(i); });
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

    for (int i = 0; i < InputCount(); ++i) {
      Input(i).previous_output = rhs.Input(i).previous_output;
      Input(i).sequence = rhs.Input(i).sequence;
      SetSignatureScript(i, rhs.SignatureScript(i));
    }
    for (int i = 0; i < OutputCount(); ++i) {
      Output(i).value = rhs.Output(i).value;
      SetPkScript(i, rhs.PkScript(i));
    }
    for (int i = 0; i < rhs.WitnessCount(); ++i) {
      ResizeComponents(i, rhs.Witness(i).Size());
      for (int j = 0; j < Witness(i).Size(); ++j) SetWitnessScript(i, j, rhs.WitnessScript(i, j));
    }
  }

 protected:
  void SetScript(ScriptArray& script_array, std::span<const uint8_t> script) {
    if (std::ssize(script) > std::numeric_limits<int>::max())
      util::ThrowOutOfRange("Script size ", script.size(), " too large.");
    data_.ResizeScriptBytes(script_array, static_cast<int>(std::ssize(script)));
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
  template <typename Data, typename Detail>
  static std::true_type Test(const TransactionViewT<Data, Detail>*);
  static std::false_type Test(...);

 public:
  static constexpr bool value = decltype(Test(std::declval<T*>()))::value;
};
template <typename T>
concept TransactionViewType = IsTransactionViewConvertible<std::remove_cvref_t<T>>::value;

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

template <typename TransactionDataT>
class TransactionIteratorT {
 public:
  static constexpr bool kIsConst = std::is_const_v<TransactionDataT>;
  using DetailCollection = std::vector<TransactionDetail>;
  using DetailIterator = std::conditional_t<kIsConst, DetailCollection::const_iterator, DetailCollection::iterator>;
  using DetailT = std::conditional_t<kIsConst, std::add_const_t<typename DetailIterator::value_type>, typename DetailIterator::value_type>;
  using View = TransactionViewT<TransactionDataT, DetailT>;

  using value_type = View;
  using difference_type = ptrdiff_t;
  using pointer = View*;
  using reference = View&;
  using iterator_category = std::forward_iterator_tag;

  TransactionIteratorT(TransactionDataT& data, DetailIterator begin) : data_(data), it_(begin) {}
  TransactionIteratorT(const TransactionIteratorT&) = default;
  TransactionIteratorT(TransactionIteratorT&&) = default;
  
  bool operator !=(const TransactionIteratorT& rhs) const {
    return it_ != rhs.it_;
  }
  bool operator ==(const TransactionIteratorT& rhs) const {
    return it_ == rhs.it_;
  }
  View* operator ->() const {
    return &(operator *());
  }
  View& operator *() const {
    if (!view_.has_value())
      view_.emplace(data_, *it_);
    return *view_;
  }
  TransactionIteratorT& operator++() {
    ++it_;
    view_.reset();
    return *this;
  }
  TransactionIteratorT operator++(int) {
    TransactionIteratorT tmp = *this;
    ++(*this);
    return tmp;
  }
  
 private:
  TransactionDataT& data_;
  DetailIterator it_;
  mutable std::optional<View> view_;
};

using TransactionIterator = TransactionIteratorT<TransactionData>;
using TransactionConstIterator = TransactionIteratorT<const TransactionData>;

}  // namespace hornet::protocol

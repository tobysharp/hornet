// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <ranges>
#include <vector>

#include "hornetlib/encoding/reader.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/iterator_range.h"

namespace hornet::protocol {

class Block {
 public:
  Block() {}
  Block(const Block&) = default;
  Block(Block&&) = default;
  Block& operator =(const Block&) = default;
  Block& operator =(Block&&) = default;

  static const Block& Genesis();

  const BlockHeader& Header() const {
    return header_;
  }
  void SetHeader(const BlockHeader& header) {
    header_ = header;
  }

  int GetTransactionCount() const {
    return std::ssize(transactions_);
  }
  TransactionView Transaction(int index) {
    return {data_, transactions_[index]};
  }
  TransactionConstView Transaction(int index) const {
    return {data_, transactions_[index]};
  }

  template <TransactionViewType View>
  void AddTransaction(const View& view) {
    TransactionDetail detail;
    TransactionView{data_, detail}.CopyFrom(view);
    transactions_.push_back(detail);
  }

  // Returns the number of weight units (WU) for the block.
  int GetWeightUnits() const {
    return 4 * serialized_bytes_ - 3 * data_.GetWitnessBytes();
  }

  // Returns the size of the block in memory, in bytes.
  int SizeBytes() const {
    int size = sizeof(*this) - sizeof(data_);
    size += transactions_.capacity() * sizeof(TransactionDetail);
    size += data_.SizeBytes();
    return size;
  }

  void Serialize(encoding::Writer& writer, bool include_witness = true) const {
    header_.Serialize(writer);
    writer.WriteVarInt(transactions_.size());
    for (const auto& tx : transactions_)
      tx.Serialize(writer, data_, include_witness);
  }

  void Deserialize(encoding::Reader& reader) {
    // There's no way for 100K transactions to fit in a 4MB block.
    constexpr size_t kUpperBoundTxInBlock = 100'000;

    const auto start = reader.GetPos();
    header_.Deserialize(reader);
    const size_t txn_count = reader.ReadVarInt();
    if (txn_count > kUpperBoundTxInBlock)
      util::ThrowOutOfRange("Too many transactions in block: ", txn_count, ".");
    transactions_.resize(txn_count);
    for (auto& tx : transactions_)
      tx.Deserialize(reader, data_);
    const auto end = reader.GetPos();

    serialized_bytes_ = end - start;
  }

  using ConstIterator = TransactionConstIterator;

  [[nodiscard]] auto Transactions() const {
    return util::MakeRange<ConstIterator>({data_, transactions_.begin()}, {data_, transactions_.end()});
  }

 private:
  BlockHeader header_;
  std::vector<TransactionDetail> transactions_;
  TransactionData data_;
  int serialized_bytes_ = 0;
};


}  // namespace hornet::protocol

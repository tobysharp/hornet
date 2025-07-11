#pragma once

#include <vector>

#include "hornetlib/encoding/reader.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::protocol {

class Block {
 public:
  Block() {}
  Block(const Block&) = default;
  Block(Block&&) = default;
  Block& operator =(const Block&) = default;
  Block& operator =(Block&&) = default;

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
  
  void Serialize(encoding::Writer& writer) const {
    header_.Serialize(writer);
    writer.WriteVarInt(transactions_.size());
    for (const auto& tx : transactions_)
      tx.Serialize(writer, data_);
  }

  void Deserialize(encoding::Reader& reader) {
    // There's no way for 100K transactions to fit in a 4MB block.
    constexpr size_t kUpperBoundTxInBlock = 100'000;

    header_.Deserialize(reader);
    const size_t txn_count = reader.ReadVarInt();
    if (txn_count > kUpperBoundTxInBlock)
      util::ThrowOutOfRange("Too many transactions in block: ", txn_count, ".");
    transactions_.resize(txn_count);
    for (auto& tx : transactions_)
      tx.Deserialize(reader, data_);
  }

 private:
  BlockHeader header_;
  std::vector<TransactionDetail> transactions_;
  TransactionData data_;
};

}  // namespace hornet::protocol

// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "hornetlib/crypto/hash.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::protocol {

// Computes the txid, which is the double-SHA256 hash of the serialized transaction,
// excluding any witness data from the serialization.
inline protocol::Hash ComputeTxid(const TransactionDetail& detail, const TransactionData& data) {
  // TODO: Create a HashWriter class that processes 64 bytes at a time for hashing.
  encoding::Writer writer;
  detail.Serialize(writer, data, false);
  const auto& buffer = writer.Buffer();
  return crypto::DoubleSha256(buffer.begin(), buffer.end());
}

}  // namespace hornet::protocol

// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <optional>

#include "hornetlib/crypto/hash.h"
#include "hornetlib/encoding/endian.h"
#include "hornetlib/encoding/reader.h"
#include "hornetlib/encoding/transfer.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/protocol/compact_target.h"
#include "hornetlib/protocol/constants.h"
#include "hornetlib/protocol/target.h"
#include "hornetlib/protocol/work.h"
#include "hornetlib/util/big_uint.h"
#include "hornetlib/util/throw.h"

namespace hornet::protocol {

class BlockHeader {
 public:
  // Determines whether the hash meets the required target constraints.
  bool IsProofOfWork() const {
    return ComputeHash() <= bits_.Expand();
  }

  // Returns the expected amount of work done to achieve the hash target.
  Work GetWork() const {
    return bits_.Expand().GetWork();
  }

  int GetVersion() const {
    return version_;
  }
  const Hash& GetPreviousBlockHash() const {
    return prev_block_;
  }
  const Hash& GetMerkleRoot() const {
    return merkle_root_;
  }
  uint32_t GetTimestamp() const {
    return timestamp_;
  }
  CompactTarget GetCompactTarget() const {
    return bits_;
  }
  uint32_t GetNonce() const {
    return nonce_;
  }

  void Serialize(encoding::Writer& w) const {
    Transfer(w, *this);
  }

  void SetVersion(int version) {
    version_ = version;
  }
  void SetPreviousBlockHash(const Hash& hash) {
    prev_block_ = hash;
  }
  void SetMerkleRoot(const Hash& hash) {
    merkle_root_ = hash;
  }
  void SetTimestamp(uint32_t timestamp) {
    timestamp_ = timestamp;
  }
  void SetCompactTarget(CompactTarget bits) {
    bits_ = bits;
  }
  void SetNonce(uint32_t nonce) {
    nonce_ = nonce;
  }

  void Deserialize(encoding::Reader& r) {
    Transfer(r, *this);
    if (txn_count_ != 0)
      util::ThrowOutOfRange("In block header, tx_count has nonzero value ", txn_count_, ".");
  }

  Hash ComputeHash() const {
    static constexpr size_t kHashedSize = 80;
    static_assert(offsetof(BlockHeader, txn_count_) - offsetof(BlockHeader, version_) ==
                  kHashedSize);
    static_assert(encoding::IsLittleEndian());
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&version_);
    return crypto::DoubleSha256(bytes, bytes + kHashedSize);
  }

 private:
  template <typename S, typename T>
  static void Transfer(S& s, T& t) {
    TransferLE4(s, t.version_);
    TransferBytes(s, util::AsSpan(t.prev_block_));
    TransferBytes(s, util::AsSpan(t.merkle_root_));
    TransferLE4(s, t.timestamp_);
    TransferObject(s, t.bits_);
    TransferLE4(s, t.nonce_);
    TransferVarInt(s, t.txn_count_);
  }

  int32_t version_;   // Block version information (note, this is signed).
  Hash prev_block_;   // The hash value of the previous block this particular block references.
  Hash merkle_root_;  // The reference to a Merkle tree collection which is a hash of all
                      // transactions related to this block.
  uint32_t
      timestamp_;  // A timestamp recording when this block was created (Will overflow in 2106).
  CompactTarget bits_;  // The calculated difficulty target being used for this block.
  uint32_t nonce_;  // The nonce used to generate this block... to allow variations of the header
                    // and compute different hashes.
  uint32_t txn_count_ = 0;  // Number of transaction entries, this value is always 0.
};

// struct HeaderDetail {
//   HeaderDetail(BlockHeader header)
//       : header(std::move(header)), hash(header.ComputeHash()), work(header.GetWork()) {}

//   BlockHeader header;
//   Hash hash;
//   Work work;
// };

}  // namespace hornet::protocol

#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "protocol/block_header.h"
#include "protocol/work.h"
#include "util/log.h"

namespace hornet::data {

class HeaderSync {
 public:
  int Accept(std::span<const protocol::BlockHeader> headers) {
    // Step 1: Compute all hashes (can be parallelized)
    std::vector<protocol::Hash> hashes;
    hashes.reserve(headers.size());
    for (const auto& header : headers) hashes.emplace_back(header.GetHash());

    // Step 2: Add all headers
    int count = 0;
    headers_.reserve(headers_.size() + headers.size());
    for (; static_cast<size_t>(count) < headers.size(); ++count) {
      if (!Accept(headers[count], hashes[count]))
        break;
    }

    LogDebug() << "Added " << count << " headers, size " << headers_.size();
    return count;
  }

  int Size() const { return headers_.size(); }

 private:
  struct Entry {
    // Block version information (note, this is signed).
    int32_t version_;
    // The reference to a Merkle tree collection which is a hash of all transactions related to this
    // block.
    protocol::Hash merkle_root_;
    // A timestamp recording when this block was created (Will overflow in 2106).
    uint32_t timestamp_;
    // The calculated difficulty target being used for this block.
    uint32_t bits_;
    // The nonce used to generate this block... to allow variations of the header and compute
    // different hashes.
    uint32_t nonce_;
  };

  bool Accept(const protocol::BlockHeader& header, const protocol::Hash& hash) {
    // Check the header is next in the chain.
    const protocol::Hash& prev_hash = header.GetPreviousBlockHash();
    if (hash_tip_ != prev_hash) return false;
    
    // Validate that the hash achieves the target PoW value.
    if (!header.IsProofOfWork()) return false;

    // TODO: Verify the version number?
    // TODO: Verify nBits is valid and matches the difficulty rules.
    // TODO: Verify the timestamp matches the MTP rule.
    
    static constexpr int kHeadersPerCheckBits = 10'000;
    if (headers_.size() % kHeadersPerCheckBits == 0) {
      // TODO: Verify nBits matches the compile-time checkpoint.
    }
  
    Entry entry = {
      .version_ = header.GetVersion(),
      .merkle_root_ = header.GetMerkleRoot(),
      .timestamp_ = header.GetTimestamp(),
      .bits_ = header.GetBits(),
      .nonce_ = header.GetNonce()
    };
    headers_.emplace_back(std::move(entry));
    cumulative_work_ += header.GetWork();
    hash_tip_ = hash;
    return true;
  }

  std::vector<Entry> headers_;
  protocol::Hash hash_tip_ = protocol::kGenesisHash;
  protocol::Work cumulative_work_;
};

}  // namespace hornet::data
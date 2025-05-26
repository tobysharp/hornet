#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "protocol/block_header.h"

namespace hornet::data {

class HeaderStore {
 public:
  enum Result { RejectOrphan, RejectDuplicate, Appended };

  Result Accept(const protocol::BlockHeader& header) {
    return Accept(header, header.GetHash(), true);
  }

  size_t AcceptMany(std::span<const protocol::BlockHeader> headers) {
    size_t count = 0;

    // Step 1: Compute all hashes (can be parallelized)
    std::vector<protocol::Hash> hashes;
    hashes.reserve(headers.size());
    for (const auto& header : headers) hashes.emplace_back(header.GetHash());

    // Step 2: Find all duplicates (can be parallelized)
    std::unordered_set<protocol::Hash> existing;
    for (const auto& hash : hashes) {
      if (hash_map_.contains(hash)) existing.emplace(hash);
    }

    // Step 3: Add all headers
    headers_.reserve(headers_.size() + headers.size());
    hash_map_.reserve(hash_map_.size() + headers.size());
    for (size_t i = 0; i < headers.size(); ++i) {
      if (existing.contains(hashes[i])) continue;
      if (Accept(headers[i], hashes[i], false) == Result::Appended) ++count;
    }
    return count;
  }

 private:
  struct HeaderNode {
    protocol::BlockHeader header;
    uint32_t parent_index = 0;
  };

  Result Accept(protocol::BlockHeader header, const protocol::Hash& hash, bool check_duplicates) {
    const auto& prev_hash = header.GetPreviousBlockHash();
    size_t parent_index = static_cast<size_t>(-1);
    if (!headers_.empty()) {
      if (hash_tip_ == prev_hash) {
        parent_index = headers_.size() - 1;
      } else {
        if (check_duplicates && hash_map_.contains(hash)) return Result::RejectDuplicate;
        const auto it = hash_map_.find(prev_hash);
        if (it == hash_map_.end()) return Result::RejectOrphan;
        parent_index = it->second;
      }
    }
    HeaderNode node;
    node.header = std::move(header);
    node.parent_index = parent_index;
    headers_.emplace_back(std::move(node));
    hash_map_.emplace(hash, headers_.size() - 1);
    hash_tip_ = hash;
    return Result::Appended;
  }

  std::vector<HeaderNode> headers_;
  std::unordered_map<protocol::Hash, size_t> hash_map_;
  protocol::Hash hash_tip_;
};

}  // namespace hornet::data
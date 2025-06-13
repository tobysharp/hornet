#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "protocol/block_header.h"
#include "protocol/work.h"
#include "util/log.h"

namespace hornet::data {

// The class HeaderStore holds all historical verified headers in a linear chain.
// Manages disk storage and memory-mapped files. Headers are only pushed to this
// object when they have been verified and have matured from tentative forks to
// confirmed heaviest chain consensus.
class HeaderStore {
 public:
  // Push headers into the store by copy.
  int Push(std::span<const protocol::BlockHeader> headers, 
           const protocol::Work& total_tip_work) {
    Assert(total_tip_work >= total_work_);
    headers_.insert(headers_.end(), headers.begin(), headers.end());
    total_work_ = total_tip_work;
    if (!headers.empty()) tip_hash_ = {};
    return Length() - 1;
  }

  // Push headers into the store via pass-by-value-and-move.
  int Push(protocol::BlockHeader header, const protocol::Work& total_tip_work) {
    Assert(total_tip_work >= total_work_);
    headers_.emplace_back(std::move(header));
    total_work_ = total_tip_work;
    tip_hash_ = {};
    return Length() - 1;
  }

  void TruncateLength(int length, const protocol::Work& total_tip_work) {
    headers_.resize(length);
    total_work_ = total_tip_work;
    tip_hash_ = {};
  }

  bool Empty() const {
    return headers_.empty();
  }

  int Length() const {
    return std::ssize(headers_);
  }

  const protocol::BlockHeader& At(int height) const {
    return headers_[height];
  }

  std::optional<protocol::BlockHeader> Tip() const {
    return headers_.empty() ? std::nullopt : std::make_optional(headers_.back());
  }

  const protocol::Hash& GetHash(int height) const {
    if (height == Length() - 1) return GetTipHash();
    return headers_[height + 1].GetPreviousBlockHash();
  }

  int GetTipHeight() const {
    return Length() - 1;
  }

  const protocol::Hash& GetTipHash() const {
    if (!tip_hash_) tip_hash_ = Tip().ComputeHash();
    return *tip_hash_;
  }

  const protocol::Work& GetTipTotalWork() const {
    return total_work_;
  }

  const protocol::BlockHeader& operator[](int height) const {
    return At(height);
  }

 private:
  std::vector<protocol::BlockHeader> headers_;
  protocol::Work total_work_;
  mutable std::optional<protocol::Hash> tip_hash_;
};

}  // namespace hornet::data
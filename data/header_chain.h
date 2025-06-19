#pragma once

#include <concepts>
#include <iterator>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "data/header_context.h"
#include "protocol/block_header.h"
#include "protocol/work.h"
#include "util/assert.h"
#include "util/log.h"

namespace hornet::data {

// The class HeaderChain holds all historical verified headers in a linear chain.
// Manages disk storage and memory-mapped files. Headers are only pushed to this
// object when they have been verified and have matured from tentative forks to
// confirmed heaviest chain consensus.
class HeaderChain {
 public:
  class UpIterator {
   public:
    // These type aliases are crucial for satisfying iterator concepts.
    using value_type = HeaderContext;
    using difference_type = std::ptrdiff_t;
    using iterator_concept = std::forward_iterator_tag;
    using reference        = const value_type&; // For iterators that return a prvalue from operator*, reference is the value_type itself.
    using pointer          = const value_type*;    

    UpIterator() : chain_(nullptr), context_(HeaderContext::Null()) {}
    explicit UpIterator(const HeaderChain& chain, std::nullopt_t)
        : chain_(&chain), context_(HeaderContext::Null()) {}
    explicit UpIterator(const HeaderChain& chain)
        : chain_(&chain),
          context_(chain.Empty() ? HeaderContext::Null()
                                 : HeaderContext{.header = *chain.Tip(),
                                                 .hash = chain.GetTipHash(),
                                                 .local_work = chain.Tip()->GetWork(),
                                                 .total_work = chain.GetTipTotalWork(),
                                                 .height = chain.Length() - 1}) {}

    const HeaderContext& operator*() const {
      return context_;
    }
    const HeaderContext* operator->() const {
      return &context_;
    }
    bool operator!=(const UpIterator& rhs) const {
      return !operator==(rhs);
    }
    bool operator==(const UpIterator& rhs) const {
      return chain_ == rhs.chain_ && context_.height == rhs.context_.height;
    }

    UpIterator& operator++() {
      context_ = (context_.height > 0) ? context_.Rewind(chain_->At(context_.height - 1))
                                       : HeaderContext::Null();
      return *this;
    }
    void operator++(int) {
      ++(*this);
    }

   private:
    const HeaderChain* chain_;
    HeaderContext context_;
  };

  UpIterator BeginFromTip() const {
    return UpIterator{*this};
  }
  UpIterator EndFromTip() const {
    return UpIterator{*this, std::nullopt};
  }
  std::ranges::subrange<UpIterator> FromTip() const {
    return {BeginFromTip(), EndFromTip()};
  }
  HeaderContext GetTipContext() const {
    return *BeginFromTip();
  }

  // Push headers into the chain by copy.
  int Push(std::span<const protocol::BlockHeader> headers, const protocol::Work& total_tip_work) {
    Assert(total_tip_work >= total_work_);
    headers_.insert(headers_.end(), headers.begin(), headers.end());
    total_work_ = total_tip_work;
    if (!headers.empty()) tip_hash_ = {};
    return Length() - 1;
  }

  // Push headers into the chain via pass-by-value-and-move.
  int Push(const protocol::BlockHeader& header, const protocol::Work& total_tip_work) {
    Assert(total_tip_work >= total_work_);
    Assert(Empty() || header.GetPreviousBlockHash() == GetTipHash());
    headers_.emplace_back(header);
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
    if (Empty()) util::ThrowOutOfRange("Tip hash requested on an empty chain.");
    if (!tip_hash_) tip_hash_ = Tip()->ComputeHash();
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
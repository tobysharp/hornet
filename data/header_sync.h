#pragma once

#include <span>
#include <vector>

#include "consensus/validator.h"
#include "data/header_timechain.h"
#include "protocol/block_header.h"
#include "util/thread_safe_queue.h"

namespace hornet::data {

class HeaderSync {
 public:
  HeaderSync(HeaderTimechain& timechain) : timechain_(timechain) {}
  HeaderSync() = delete;

  // Push new headers into the unverified queue for later processing, which
  // could be in the same thread or a different thread.
  int Receive(std::span<const protocol::BlockHeader> headers) {  // Copy
    queue_.Push(Batch{headers.begin(), headers.end()});
    return std::ssize(headers);
  }

  int Receive(std::vector<protocol::BlockHeader>&& headers) {  // Move
    queue_.Push(std::move(headers));
    return std::ssize(headers);
  }

  // Returns true if there is validation work to be done, i.e. queued headers.
  bool HasPendingWork() const {
    return !queue_.Empty();
  }

  // Perform validation on the queued headers in the current thread. Works
  // until all work is done or the given timeout (in ms) expires. This method
  // also performs the maturation from the tree structure to the header chain.
  void Validate(const util::Timeout& timeout = util::Timeout::Infinite());

 private:
  using Batch = std::vector<protocol::BlockHeader>;

  void Fail() {}  // TODO

  // Headers start out being pushed into an unverified queue pipeline.
  util::ThreadSafeQueue<Batch> queue_;
  HeaderTimechain& timechain_;
  consensus::Validator validator_;
};

}  // namespace hornet::data
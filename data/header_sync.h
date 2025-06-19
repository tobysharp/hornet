#pragma once

#include <span>
#include <thread>
#include <vector>

#include "consensus/validator.h"
#include "data/header_timechain.h"
#include "protocol/block_header.h"
#include "util/thread_safe_queue.h"

namespace hornet::data {

class HeaderSync {
 public:
  HeaderSync(HeaderTimechain& timechain);
  ~HeaderSync();

  // Push new headers into the unverified queue for later processing, which
  // could be in the same thread or a different thread.
  int Receive(std::span<const protocol::BlockHeader> headers) {  // Copy
    queue_.Push(Batch{headers.begin(), headers.end()});
    return std::ssize(headers);
  }

  int Receive(std::vector<protocol::BlockHeader>&& headers) {  // Move
    int size = std::ssize(headers);
    queue_.Push(std::move(headers));
    return size;
  }

  // Returns true if there is validation work to be done, i.e. queued headers.
  bool HasPendingWork() const {
    return !queue_.Empty();
  }


 private:
  using Batch = std::vector<protocol::BlockHeader>;
  enum Result { Stopped, Timeout, ConsensusError };

  // Perform validation on the queued headers in the current thread. Works
  // until all work is done or the given timeout (in ms) expires. This method
  // also performs the maturation from the tree structure to the header chain.
  Result Validate(const util::Timeout& timeout = util::Timeout::Infinite());

  void WorkerThreadLoop();
  void Fail() {}  // TODO

  // Headers start out being pushed into an unverified queue pipeline.
  util::ThreadSafeQueue<Batch> queue_;
  HeaderTimechain& timechain_;
  consensus::Validator validator_;
  std::thread worker_thread_;
};

}  // namespace hornet::data
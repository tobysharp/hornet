#pragma once

#include <thread>

#include "hornetlib/data/utxo/memory_age.h"
#include "hornetlib/util/thread_safe_queue.h"

namespace hornet::data::utxo {

class Compacter {
 public:
  Compacter() : thread_([this] { Run(); }) {}
  ~Compacter() {
    jobs_.Stop();
    if (thread_.joinable()) thread_.join();
  }
  void EnqueueMerge(MemoryAge* src, MemoryAge* dst) {
    jobs_.Push(Job{src, dst});
  }

 private:
  struct Job {
    MemoryAge* src_;
    MemoryAge* dst_;
  };

  void Run() {
    while (auto job = jobs_.WaitPop()) job->src_->Merge(job->dst_);
  }

  ThreadSafeQueue<Job> jobs_;
  std::thread thread_;
};

}  // namespace hornet::data::utxo

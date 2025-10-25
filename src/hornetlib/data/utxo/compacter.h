#pragma once

#include <thread>
#include <vector>

#include "hornetlib/data/utxo/memory_age.h"
#include "hornetlib/util/thread_safe_queue.h"

namespace hornet::data::utxo {

class Compacter {
 public:
  Compacter(int count) {
    threads_.reserve(count);
    for (int i = 0; i < count; ++i)
      threads_.emplace_back([this] { Run(); });
  }
  ~Compacter() {
    jobs_.Stop();
    for (auto& thread : threads_)
      if (thread.joinable()) thread.join();
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
  std::vector<std::thread> threads_;
};

}  // namespace hornet::data::utxo

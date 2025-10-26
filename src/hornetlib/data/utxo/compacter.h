#pragma once

#include <thread>
#include <vector>

#include "hornetlib/data/utxo/memory_age.h"
#include "hornetlib/util/thread_safe_queue.h"

namespace hornet::data::utxo {

class Compacter {
 public:
  Compacter(int ages) {
    threads_.reserve(ages);
    for (int i = 0; i < ages; ++i)
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
    MemoryAge* src;
    MemoryAge* dst;
  };

  void Run() {
    while (auto job = jobs_.WaitPop()) job->src->Merge(job->dst);
  }

  util::ThreadSafeQueue<Job> jobs_;
  std::vector<std::thread> threads_;
};

}  // namespace hornet::data::utxo

#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "hornetlib/data/utxo/database.h"
#include "hornetlib/data/utxo/joiner.h"

namespace hornet::data::utxo {

class SpendPipeline {
 public:
  explicit SpendPipeline(Database& db, int num_threads) 
      : db_(db) {
    for (int i = 0; i < num_threads; ++i)
      workers_.emplace_back([this] { WorkerLoop(); });
  }

  ~SpendPipeline() {
    Stop();
  }

  // Creates a SpendJoiner, adds it to the pipeline, and returns it so it can be
  // wrapped in a DatabaseView for the consumer.
  std::shared_ptr<SpendJoiner> Add(std::shared_ptr<const protocol::Block> block, int height) {
    if (abort_) throw SpendJoiner::CancelledException{};
    auto joiner = std::make_shared<SpendJoiner>(db_, std::move(block), height);
    {
      std::lock_guard lock(mutex_);
      std::erase_if(active_joiners_, [](const auto& weak) { return weak.expired(); });
      active_joiners_.push_back(joiner);
      ready_queue_.push(joiner);
    }
    cv_.notify_one();
    return joiner;
  }

  void Stop() {
    abort_ = true;
    {
      std::lock_guard lock(mutex_);
      for (const auto& weak : active_joiners_)
        if (auto joiner = weak.lock())
          joiner->Cancel();
      active_joiners_.clear();
    }
    cv_.notify_all();
    for (auto& t : workers_) {
      if (t.joinable()) t.join();
    }
    workers_.clear();
  }

 private:
  void WorkerLoop() {
    while (true) {
      std::shared_ptr<SpendJoiner> job;
      {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [&] { return abort_ || !ready_queue_.empty(); });
        if (abort_) return;
        job = ready_queue_.top();
        ready_queue_.pop();
      }

      Assert(job->IsAdvanceReady());
      job->Advance();
      const SpendJoiner::State state = job->GetState();

      // If we just appended, we may have unblocked other jobs.
      if (state == SpendJoiner::State::Appended)
        WakeBlockedJobs();

      // If the job is finished (or failed), drops our reference.
      if (state != SpendJoiner::State::Error && !job->IsJoinReady()) {
        std::unique_lock lock(mutex_);
        if (job->IsAdvanceReady()) {
          // Ready for more work immediately.
          ready_queue_.push(std::move(job));
          cv_.notify_one();
        } else {
          // Blocked (waiting for DB height).
          blocked_list_.push_back(std::move(job));
        }
      }
    }
  }

  void WakeBlockedJobs() {
    std::lock_guard lock(mutex_);
    // Scan the blocked list for jobs that are now ready.
    auto it = blocked_list_.begin();
    while (it != blocked_list_.end()) {
      if ((*it)->IsAdvanceReady()) {
        ready_queue_.push(std::move(*it));
        it = blocked_list_.erase(it);
        cv_.notify_one();
      } else {
        ++it;
      }
    }
  }

  struct OrderByHeight {
    // Min-heap: lowest height (oldest block) has highest priority.
    bool operator()(const std::shared_ptr<SpendJoiner>& a, const std::shared_ptr<SpendJoiner>& b) const {
      return a->GetHeight() > b->GetHeight();
    }
  };

  Database& db_;
  std::vector<std::thread> workers_;
  
  std::priority_queue<std::shared_ptr<SpendJoiner>, 
                      std::vector<std::shared_ptr<SpendJoiner>>, 
                      OrderByHeight> ready_queue_;
                      
  std::vector<std::shared_ptr<SpendJoiner>> blocked_list_;
  std::vector<std::weak_ptr<SpendJoiner>> active_joiners_;

  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<bool> abort_ = false;
};

}  // namespace hornet::data::utxo

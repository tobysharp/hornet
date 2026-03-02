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
#include "hornetlib/util/heap.h"

namespace hornet::data::utxo {

class SpendPipeline {
 public:
  // TODO: Switch to std::move_only_function when libc++ adds support (C++23).
  using CompleteCallback = std::function<void(std::shared_ptr<SpendJoiner>)>;

  explicit SpendPipeline(Database& db, int num_threads)
      : db_(db) {
    for (int i = 0; i < num_threads; ++i) workers_.emplace_back([this] { WorkerLoop(); });
  }

  ~SpendPipeline() {
    Abort();
    for (auto& t : workers_) {
      if (t.joinable()) t.join();
    }
    workers_.clear();
  }

  // Creates a SpendJoiner, adds it to the pipeline, and returns it so it can be
  // wrapped in a DatabaseView for the consumer.
  std::shared_ptr<SpendJoiner> Add(std::shared_ptr<const protocol::Block> block, int height,
                                   bool assume_valid = false, CompleteCallback callback = {}) {
    if (abort_) throw SpendJoiner::CancelledException{};
    auto joiner = std::make_shared<SpendJoiner>(db_, std::move(block), height, assume_valid);
    {
      std::lock_guard lock(mutex_);
      std::erase_if(active_joiners_, [](const auto& weak) { return weak.expired(); });
      active_joiners_.push_back(joiner);
      ready_queue_.Push(Job{joiner, std::move(callback)});
    }
    cv_.notify_one();
    return joiner;
  }

  void Abort() {
    {
      std::lock_guard lock(mutex_);
      abort_ = true;
      for (const auto& weak : active_joiners_)
        if (auto joiner = weak.lock()) joiner->Cancel();
      active_joiners_.clear();
    }
    cv_.notify_all();
  }

  bool IsStalled() const {
    std::unique_lock lock(mutex_);
    return ready_queue_.Empty() && !blocked_list_.empty() && busy_workers_ == 0;
  }

  struct Metrics {
    util::Timer advance_;
    SpendJoiner::Metrics joiner_metrics_;
  };

  const Metrics& GetMetrics() const { return metrics_; }

 private:
  struct Job {
    std::shared_ptr<SpendJoiner> joiner;
    CompleteCallback on_complete;

    // Min-heap: lowest height (oldest block) has highest priority.
    bool operator<(const Job& rhs) const { return joiner->GetHeight() > rhs.joiner->GetHeight(); }
  };

  void WorkerLoop() {
    while (true) {
      Job job;
      {
        std::unique_lock lock(mutex_);

        // Prevent lost wakeups by checking the blocked list before sleeping.
        WakeBlockedJobsInternal();

        cv_.wait(lock, [&] { return abort_ || !ready_queue_.Empty(); });
        if (abort_) return;
        job = ready_queue_.Pop();
        ++busy_workers_;
      }

      try {
        Assert(job.joiner->IsAdvanceReady());

        auto advance_timer = metrics_.advance_.AddScoped();
        const auto [state, action] = job.joiner->Advance();
        advance_timer.End();

        // If we just appended, we may have unblocked other jobs.
        if (state == SpendJoiner::State::Appended) WakeBlockedJobs();

        if (action == SpendJoiner::Action::Advance) {
          std::unique_lock lock(mutex_);
          ready_queue_.Push(std::move(job));
          cv_.notify_one();
        } else if (action == SpendJoiner::Action::Wait) {
          std::unique_lock lock(mutex_);
          blocked_list_.push_back(std::move(job));
        } else if (job.on_complete) {
          metrics_.joiner_metrics_ += job.joiner->GetMetrics();
          job.on_complete(std::move(job.joiner));
        }
      } catch (const SpendJoiner::CancelledException&) {
        // Job was cancelled, ignore.
      }
      --busy_workers_;
    }
  }

  void WakeBlockedJobsInternal() {
    // Scan the blocked list for jobs that are now ready.
    auto it = blocked_list_.begin();
    while (it != blocked_list_.end()) {
      if (it->joiner->IsAdvanceReady()) {
        ready_queue_.Push(std::move(*it));
        it = blocked_list_.erase(it);
        cv_.notify_one();
      } else {
        ++it;
      }
    }
  }

  void WakeBlockedJobs() {
    std::lock_guard lock(mutex_);
    WakeBlockedJobsInternal();
  }

  Database& db_;
  std::vector<std::thread> workers_;

  util::Heap<Job> ready_queue_;
  std::vector<Job> blocked_list_;
  std::vector<std::weak_ptr<SpendJoiner>> active_joiners_;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool abort_ = false;
  std::atomic<int> busy_workers_ = 0;
  Metrics metrics_;
};

}  // namespace hornet::data::utxo

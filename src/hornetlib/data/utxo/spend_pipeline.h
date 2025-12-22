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
  using CompleteCallback = std::function<void(std::shared_ptr<SpendJoiner>)>;

  explicit SpendPipeline(Database& db, int num_threads, CompleteCallback on_complete = nullptr) 
      : db_(db), on_complete_(std::move(on_complete)) {
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
    {
      std::lock_guard lock(mutex_);
      abort_ = true;
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

  bool IsStalled() const {
    std::unique_lock lock(mutex_);
    return ready_queue_.empty() && !blocked_list_.empty() && busy_workers_ == 0;
  }

  struct DetailedMetrics {
    long long total_advance_time_ns;
    long long total_advance_calls;
    long long parse_time_ns;
    long long append_time_ns;
    long long query_time_ns;
    long long fetch_time_ns;
  };

  DetailedMetrics GetMetrics() const {
    return {
      total_advance_time_ns.load(), 
      total_advance_calls.load(),
      total_parse_time_ns.load(),
      total_append_time_ns.load(),
      total_query_time_ns.load(),
      total_fetch_time_ns.load()
    };
  }

 private:
  void WorkerLoop() {
    while (true) {
      std::shared_ptr<SpendJoiner> job;
      {
        std::unique_lock lock(mutex_);

        // Prevent lost wakeups by checking the blocked list before sleeping.
        if (ready_queue_.empty() && !blocked_list_.empty())
          WakeBlockedJobsInternal();

        cv_.wait(lock, [&] { return abort_ || !ready_queue_.empty(); });
        if (abort_) return;
        job = ready_queue_.top();
        ready_queue_.pop();
        ++busy_workers_;
      }

      Assert(job->IsAdvanceReady());
      
      auto start = std::chrono::high_resolution_clock::now();
      const auto [state, action] = job->Advance();
      auto end = std::chrono::high_resolution_clock::now();
      total_advance_time_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
      total_advance_calls++;

      // If we just appended, we may have unblocked other jobs.
      if (state == SpendJoiner::State::Appended)
        WakeBlockedJobs();

      if (action == SpendJoiner::Action::Advance) {
        std::unique_lock lock(mutex_);
        ready_queue_.push(std::move(job));
        cv_.notify_one();
      } else if (action == SpendJoiner::Action::Wait) {
        std::unique_lock lock(mutex_);
        blocked_list_.push_back(std::move(job));
      } else if (on_complete_) {
        auto m = job->GetMetrics();
        total_parse_time_ns += m.parse_time_ns;
        total_append_time_ns += m.append_time_ns;
        total_query_time_ns += m.query_time_ns;
        total_fetch_time_ns += m.fetch_time_ns;
        on_complete_(std::move(job));
      }
      --busy_workers_;
    }
  }

  void WakeBlockedJobsInternal() {
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

  void WakeBlockedJobs() {
    std::lock_guard lock(mutex_);
    WakeBlockedJobsInternal();
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

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool abort_ = false;
  CompleteCallback on_complete_;
  std::atomic<int> busy_workers_ = 0;

  std::atomic<long long> total_advance_time_ns{0};
  std::atomic<long long> total_advance_calls{0};
  std::atomic<long long> total_parse_time_ns{0};
  std::atomic<long long> total_append_time_ns{0};
  std::atomic<long long> total_query_time_ns{0};
  std::atomic<long long> total_fetch_time_ns{0};
};

}  // namespace hornet::data::utxo

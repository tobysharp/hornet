#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/validate_api.h"
#include "hornetlib/data/key.h"
#include "hornetlib/data/timechain.h"
#include "hornetlib/data/utxo/database.h"
#include "hornetlib/data/utxo/database_view.h"
#include "hornetlib/data/utxo/joiner.h"
#include "hornetlib/data/utxo/spend_pipeline.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/util/assert.h"
#include "hornetlib/util/heap.h"
#include "hornetlib/util/log.h"
#include "hornetlib/util/thread_safe_queue.h"
#include "hornetlib/util/throw.h"
#include "hornetlib/util/timeout.h"

namespace hornet::node::sync {

class ValidationPipeline {
 public:
  // TODO: Switch to std::move_only_function when libc++ adds support (C++23).
  using CompleteCallback =
      std::function<void(const std::shared_ptr<const protocol::Block>&, const data::Key&, consensus::Result)>;

  // Constructs the validation pipeline.
  // pipeline_depth: The number of blocks that can be processed concurrently.
  // This determines the number of threads in both the validation and spend pipelines.
  // window: The maximum number of block heights ahead of the retirement frontier that may be admitted at once.
  ValidationPipeline(data::Timechain& timechain, data::utxo::Database& db, int pipeline_depth = 8, int window = 0)
      : timechain_(timechain),
        spend_pipeline_(db, pipeline_depth),
        next_complete_height_(db.GetContiguousLength()),
        window_(window <= 0 ? pipeline_depth * 4 : window) {
    for (int i = 0; i < pipeline_depth; ++i) {
      workers_.emplace_back([this] { WorkerLoop(); });
    }
  }

  ~ValidationPipeline() {
    Abort();
    for (auto& t : workers_)
      if (t.joinable()) t.join();
    workers_.clear();
  }

  void Abort() {
    {
      std::lock_guard lock{wait_mutex_};
      stopping_ = true;
      submit_cv_.notify_all();
      wait_cv_.notify_all();
    }
    queue_.Stop();
    spend_pipeline_.Abort();
  }

  // Submits a block for validation. Can be out of height order.
  void Submit(std::shared_ptr<const protocol::Block> block, int height, CompleteCallback on_complete) {
    if (height == 0) util::ThrowInvalidArgument("ValidationPipeline::Submit: Genesis block should not be submitted.");

    {
      std::unique_lock lock{wait_mutex_};

      // Admit the block only once its height lies within `window_` of the retirement frontier.
      // The frontier block is always within the window, so a missing lower block cannot wedge the
      // pipeline; blocks further ahead wait here, applying back-pressure until earlier blocks retire.
      // TryRetire advances the frontier and signals submit_cv_ under wait_mutex_, and Abort sets
      // stopping_ likewise, so this predicate never misses a wakeup.
      submit_cv_.wait(lock, [&] { return stopping_ || height < GetAdmissibleHeightLimit(); });

      if (stopping_) return;
      ++active_count_;
    }
    spend_pipeline_.Add(block, height, false,
                        [this, callback = std::move(on_complete)](std::shared_ptr<data::utxo::SpendJoiner> joiner) {
                          ++validation_pending_;
                          queue_.Push({joiner->GetHeight(), joiner->GetBlock(), std::move(joiner), callback});
                        });
  }

  bool Wait(const util::Timeout& timeout = util::Timeout::Infinite()) {
    std::unique_lock lock{wait_mutex_};
    auto predicate = [this] { return active_count_ == 0 || stopping_; };
    if (timeout.IsInfinite()) {
      wait_cv_.wait(lock, predicate);
      return true;
    } else return wait_cv_.wait_until(lock, timeout.Deadline(), predicate);
  }

  std::pair<long long, long long> GetValidationMetrics() const {
    return {total_validate_time_ns.load(), total_validate_calls.load()};
  }

  const data::utxo::SpendPipeline::Metrics& GetSpendMetrics() const { return spend_pipeline_.GetMetrics(); }

  // The exclusive upper bound on currently-admissible block heights: a block is admitted once its
  // height is below this (retirement frontier + window).
  int GetAdmissibleHeightLimit() const { return next_complete_height_ + window_; }

 private:
  struct Job {
    int height;
    std::shared_ptr<const protocol::Block> block;
    std::shared_ptr<data::utxo::SpendJoiner> joiner;
    CompleteCallback on_complete;
  };

  struct JobResult {
    int height;
    std::shared_ptr<const protocol::Block> block;
    consensus::Result result;
    CompleteCallback on_complete;

    // Lesser priority is given to the greater height.
    bool operator<(const JobResult& rhs) const { return height > rhs.height; }
  };

  void WorkerLoop() {
    std::optional<Job> job;
    while ((job = queue_.WaitPop())) {
      try {
        // Perform consensus validation for the current job, and store the result.
        auto start = std::chrono::high_resolution_clock::now();
        const auto result = Validate(*job);
        auto end = std::chrono::high_resolution_clock::now();
        total_validate_time_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        total_validate_calls++;

        {
          std::lock_guard lock{retire_mutex_};
          completed_.Push(JobResult{job->height, std::move(job->block), result, std::move(job->on_complete)});
        }

        // Retire completions in order as they are ready.
        TryRetire();

      } catch (const data::utxo::SpendJoiner::CancelledException& e) {
        // Job was cancelled, presumably due to shutdown.
        {
          std::lock_guard wait_lock{wait_mutex_};
          if (--active_count_ == 0) wait_cv_.notify_all();
        }
        submit_cv_.notify_all();
        break;
      }
      --validation_pending_;
    }
  }

  // Perform consensus validation for one block. Can be out of height order.
  consensus::Result Validate(const Job& job) {
    const auto& block = *(job.block);
    const auto headers = timechain_.ReadHeaders();
    const auto parent_it = headers->FindStable(job.height - 1, block.Header().GetPreviousBlockHash());
    if (!parent_it) return consensus::Error::Header_ParentNotFound;
    const auto ancestry_view = headers->GetValidationView(parent_it);
    const data::utxo::DatabaseView utxo{job.joiner};
    return consensus::ValidateBlock(block, *parent_it, *ancestry_view, GetCurrentTime(), utxo);
  }

  // Retires completed jobs in height order, if we can take the retirement lock.
  void TryRetire() {
    std::unique_lock lock{retire_mutex_, std::try_to_lock};
    if (!lock.owns_lock()) return;  // Someone else has the retire lock, leave them to it.

    while (!completed_.Empty() && completed_.Top().height == next_complete_height_) {
      const auto item = completed_.Pop();
      lock.unlock();
      if (item.on_complete)
        item.on_complete(item.block, {item.height, item.block->Header().ComputeHash()}, item.result);
      {
        // Advance the frontier and signal submitters under wait_mutex_, so a Submit that just
        // evaluated the window predicate cannot sleep through this wakeup.
        std::lock_guard wait_lock{wait_mutex_};
        next_complete_height_ = item.height + 1;
        if (--active_count_ == 0) wait_cv_.notify_all();
        submit_cv_.notify_all();
      }
      lock.lock();
    }
  }

  // Returns the current epoch time in milliseconds for consensus validation.
  static int64_t GetCurrentTime() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

  data::Timechain& timechain_;
  data::utxo::SpendPipeline spend_pipeline_;

  util::ThreadSafeQueue<Job> queue_;
  std::vector<std::thread> workers_;

  std::mutex retire_mutex_;
  std::atomic<int> next_complete_height_;  // Next height awaiting retirement (the window's lower bound).
  util::Heap<JobResult> completed_;
  int window_;  // Max heights admissible ahead of next_complete_height_.
  int active_count_ = 0;
  std::atomic<int> validation_pending_ = 0;
  std::mutex wait_mutex_;
  std::condition_variable wait_cv_;
  std::condition_variable submit_cv_;
  bool stopping_ = false;  // Set by Abort to release blocked Submit/Wait calls during shutdown.

  std::atomic<long long> total_validate_time_ns{0};
  std::atomic<long long> total_validate_calls{0};
};

}  // namespace hornet::node::sync

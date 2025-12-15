#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "hornetlib/consensus/validate_api.h"
#include "hornetlib/data/timechain.h"
#include "hornetlib/data/utxo/database.h"
#include "hornetlib/data/utxo/database_view.h"
#include "hornetlib/data/utxo/joiner.h"
#include "hornetlib/data/utxo/spend_pipeline.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/util/thread_safe_queue.h"
#include "hornetlib/util/throw.h"
#include "hornetlib/util/timeout.h"

namespace hornet::node::sync {

class ValidationPipeline {
 public:
  using CompleteCallback =
      std::function<void(const std::shared_ptr<const protocol::Block>&, int, consensus::Result)>;

  // Constructs the validation pipeline.
  // pipeline_depth: The number of blocks that can be processed concurrently.
  // This determines the number of threads in both the validation and spend pipelines.
  ValidationPipeline(data::Timechain& timechain, data::utxo::Database& db,
                     CompleteCallback callback, int pipeline_depth = 8)
      : timechain_(timechain), on_complete_(std::move(callback)), spend_pipeline_(db, pipeline_depth) {
    for (int i = 0; i < pipeline_depth; ++i) {
      workers_.emplace_back([this] { WorkerLoop(); });
    }
  }

  ~ValidationPipeline() {
    queue_.Stop();
    spend_pipeline_.Stop();
    for (auto& t : workers_)
      if (t.joinable()) t.join();
  }

  // Submits a block for validation. Can be out of height order.
  void Submit(std::shared_ptr<const protocol::Block> block, int height) {
    if (height == 0)
      util::ThrowInvalidArgument(
          "ValidationPipeline::Submit: Genesis block should not be submitted.");
    ++active_count_;
    auto joiner = spend_pipeline_.Add(block, height);
    queue_.Push({height, std::move(block), std::move(joiner)});
  }

  bool Wait(const util::Timeout& timeout) {
    if (active_count_ == 0) return true;

    std::unique_lock lock{wait_mutex_};
    if (timeout.IsInfinite()) {
      wait_cv_.wait(lock, [this] { return active_count_ == 0; });
      return true;
    } else
      return wait_cv_.wait_until(lock, timeout.Deadline(), [this] { return active_count_ == 0; });
  }

 private:
  struct Job {
    int height;
    std::shared_ptr<const protocol::Block> block;
    std::shared_ptr<data::utxo::SpendJoiner> joiner;
  };

  struct JobResult {
    int height;
    std::shared_ptr<const protocol::Block> block;
    consensus::Result result;

    // Lesser priority is given to the greater height.
    bool operator<(const JobResult& rhs) const { return height > rhs.height; }
  };

  void WorkerLoop() {
    std::optional<Job> job;
    while ((job = queue_.WaitPop())) {
      try {
        // If this validation job doesn't yet have all its dependencies ready, and there is other
        // work to be done, defer this job until later in preference of a more productive work item.
        if (!job->joiner->IsJoinReady() && !queue_.Empty()) {
          queue_.Push(std::move(*job));
          continue;
        }

        // Perform consensus validation for the current job, and store the result.
        const auto result = Validate(*job);
        {
          std::lock_guard lock{retire_mutex_};
          completed_.push(JobResult{job->height, std::move(job->block), result});
        }

        // Retire completions in order as they are ready.
        TryRetire();

      } catch (const data::utxo::SpendJoiner::CancelledException& e) {
        // Job was cancelled, presumably due to shutdown.
        break;
      }
    }
  }

  // Perform consensus validation for one block. Can be out of height order.
  consensus::Result Validate(const Job& job) {
    const auto& block = *(job.block);
    const auto headers = timechain_.ReadHeaders();
    const auto parent_it =
        headers->FindStable(job.height - 1, block.Header().GetPreviousBlockHash());
    const auto ancestry_view = headers->GetValidationView(parent_it);
    const data::utxo::DatabaseView utxo{job.joiner};
    return consensus::ValidateBlock(block, *parent_it, *ancestry_view, GetCurrentTime(), utxo);
  }

  // Retires completed jobs in height order, if we can take the retirement lock.
  void TryRetire() {
    std::unique_lock lock{retire_mutex_, std::try_to_lock};
    if (!lock.owns_lock()) return;  // Someone else has the retire lock, leave them to it.

    for (; !completed_.empty() && completed_.top().height == next_complete_height_;
         ++next_complete_height_) {
      const auto item = std::move(completed_.top());
      completed_.pop();

      lock.unlock();
      on_complete_(item.block, item.height, item.result);
      if (--active_count_ == 0) {
        std::lock_guard wait_lock{wait_mutex_};
        wait_cv_.notify_all();
      }
      lock.lock();
    }
  }

  // Returns the current epoch time in milliseconds for consensus validation.
  static int64_t GetCurrentTime() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

  data::Timechain& timechain_;
  CompleteCallback on_complete_;
  data::utxo::SpendPipeline spend_pipeline_;

  util::ThreadSafeQueue<Job> queue_;
  std::vector<std::thread> workers_;

  std::mutex retire_mutex_;
  int next_complete_height_ = 1;  // Genesis is never validated.
  std::priority_queue<JobResult, std::vector<JobResult>> completed_;

  std::atomic<int> active_count_ = 0;
  std::mutex wait_mutex_;
  std::condition_variable wait_cv_;
};

}  // namespace hornet::node::sync

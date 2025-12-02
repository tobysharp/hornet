#pragma once

#include <memory>
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

namespace hornet::node::sync {

class ValidationPipeline {
 public:
  using CompleteCallback = std::function<void(const std::shared_ptr<const protocol::Block>&,
                                              int, consensus::Result)>;

  // Constructs the validation pipeline.
  // pipeline_depth: The number of blocks that can be processed concurrently.
  // This determines the number of threads in both the validation and spend pipelines.
  ValidationPipeline(data::Timechain& timechain, data::utxo::Database& db,
                     CompleteCallback callback, int pipeline_depth = 8)
      : timechain_(timechain), on_complete_(callback), spend_pipeline_(db, pipeline_depth) {
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

  // Submits a block for validation.
  void Submit(std::shared_ptr<const protocol::Block> block, int height) {
    auto joiner = spend_pipeline_.Add(block, height);
    queue_.Push({height, std::move(block), std::move(joiner)});
  }

 private:
  struct Job {
    int height;
    std::shared_ptr<const protocol::Block> block;
    std::shared_ptr<data::utxo::SpendJoiner> joiner;
  };

  void WorkerLoop() {
    std::optional<Job> job;
    while ((job = queue_.WaitPop())) {
      try {
        on_complete_(job->block, job->height, Process(*job));
      } catch (const data::utxo::SpendJoiner::CancelledException& e) {
        // Job was cancelled, presumably due to shutdown.
        break;
      }
    }
  }

  consensus::Result Process(const Job& job) {
    const auto& block = *(job.block);
    const auto headers = timechain_.ReadHeaders();
    const auto parent_it = headers->FindStable(job.height - 1, block.Header().GetPreviousBlockHash());
    Assert(parent_it);

    const auto ancestry_view = headers->GetValidationView(parent_it);
    return consensus::ValidateNonSpending(block, *ancestry_view).AndThen([&] {
      return consensus::ValidateSpending(block, data::utxo::DatabaseView{job.joiner}, job.height);
    });
  }

  data::Timechain& timechain_;
  CompleteCallback on_complete_;
  data::utxo::SpendPipeline spend_pipeline_;

  util::ThreadSafeQueue<Job> queue_;
  std::vector<std::thread> workers_;
};

}  // namespace hornet::node::sync

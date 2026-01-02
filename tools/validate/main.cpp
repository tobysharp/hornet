// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include <chrono>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include "options.h"
#include "readers.h"
#include "hornetlib/data/block_io.h"
#include "hornetlib/data/timechain.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/util/timer.h"
#include "hornetnodelib/controller.h"
#include "hornetnodelib/net/constants.h"
#include "hornetnodelib/net/tcp_notification_sink.h"
#include "hornetnodelib/sync/validation_pipeline.h"
#include "hornetnodelib/util/command_line_parser.h"

using namespace hornet;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

static std::atomic<bool> is_abort{false};
static std::unique_ptr<node::net::TcpNotificationSink> tcp_sink;

void HandleSignal(int) {
  is_abort = true;
  std::cout << "Aborting" << std::endl;
}

enum Operation { Op_Headers, Op_Blocks, Op_Submit, Op_Wait, Op_Total, Op_Count };
const char* kOpNames[] = {"Headers", "Blocks", "Submit", "Wait", "Total", "Count"};
using Metrics = util::Metrics<Op_Count>;

void LoadHeaders(const Options& options, Metrics* metrics, data::Timechain* timechain) {
  metrics->Add(Op_Headers, [&] {
    LogDebug() << "Reading headers [" << 1 << ", " << options.length << ")...";
    CoreReader<true> reader{options.blocks_dir};
    std::unordered_map<protocol::Hash, const protocol::BlockHeader> cache;
    auto headers = timechain->WriteHeaders();
    std::optional<protocol::BlockHeader> header = std::make_optional<protocol::BlockHeader>();
    reader >> header;  // Read genesis.
    while (headers->ChainLength() < options.length) {
      const auto tip = headers->ChainTip();
      // Check if required block is in the cache.
      if (const auto it = cache.find(tip->hash); it != cache.end()) {
        header.emplace(it->second);
        cache.erase(it);
      } else if (reader) {  // Otherwise read a block from file.
        header.emplace();
        reader >> header;
        if (!header) break;
        // If it's not the next block we need, put in in the cache instead.
        if (header->GetPreviousBlockHash() != tip->hash) {
          cache.insert(std::pair{header->GetPreviousBlockHash(), *header});
          header.reset();
        }
      }
      // If we found a block to add on this turn, add it now.
      if (header) timechain->AddHeader(tip, tip->Extend(*header));
    }
  });
  std::cout << "Loaded " << timechain->ReadHeaders()->ChainLength() << " headers." << std::endl;
}

void PrintMetrics(const Metrics& metrics, int length,
                  const node::sync::ValidationPipeline& pipeline) {
  std::cout << "Total time " << metrics[Op_Total].Seconds() << " ("
            << (length / metrics[Op_Total].Seconds().count()) << " blocks/s)" << std::endl;
  std::cout << "including " << std::endl;
  for (int op : {Op_Blocks, Op_Submit, Op_Wait}) {
    std::cout << "\t" << metrics[op].Seconds() << " " << kOpNames[op]
              << " (CPU: " << metrics[op].CpuSeconds() << ")" << std::endl;
  }

  auto [val_time, val_calls] = pipeline.GetValidationMetrics();
  const auto& spend_metrics = pipeline.GetSpendMetrics();

  std::cout << "Metrics:" << std::endl;
  std::cout << "  Validation: " << (val_time / 1e9) << "s total ("
            << (val_calls > 0 ? val_time / val_calls : 0) << " ns/call)" << std::endl;
  std::cout << "  Spend:      " << spend_metrics.advance_.Seconds() << " total ("
            << (spend_metrics.advance_.Instances() > 0
                    ? spend_metrics.advance_.Nanoseconds().count() /
                          spend_metrics.advance_.Instances()
                    : 0)
            << " ns/call)" << std::endl;
  static const char* kJoinerNames[] = {"Parse", "Append", "Query", "Fetch", "Join"};
  for (int i = 0; i < data::utxo::SpendJoiner::Time_Count; ++i) {
    std::cout << "    " << std::setw(10) << std::left << kJoinerNames[i] << ": "
              << spend_metrics.joiner_metrics_[i].Seconds() << std::endl;
  }
}

int main(int argc, char** argv) {
  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);

  Options options;
  node::util::CommandLineParser parser("Hornet Validate Tool", "0.0.1");
  parser.AddOption("notifytcp", &options.notify_tcp_port,
                   "Send notifications over TCP to the specified port");
  parser.AddOption("blocksdir", &options.blocks_dir, "Directory to load block files");
  parser.AddOption("datadir", &options.data_dir, "Directory to store database files");
  parser.AddOption("length", &options.length, "Number of blocks to validate", 1'000'000);

  if (!parser.Parse(argc, argv)) return 1;

  if (options.notify_tcp_port > 0) {
    try {
      tcp_sink = std::make_unique<node::net::TcpNotificationSink>(node::net::kLocalhost,
                                                                  options.notify_tcp_port);
      util::SetNotificationSink([](auto x) { return (*tcp_sink)(x); });
    } catch (const std::exception& e) {
      std::cerr << "Could not connect the TCP notification sink to port " << options.notify_tcp_port
                << ":" << std::endl;
      std::cerr << e.what() << std::endl;
      return -1;
    }
  }

  fs::remove_all(options.data_dir);
  fs::create_directories(options.data_dir);
  if (!fs::is_directory(options.data_dir) || !fs::is_empty(options.data_dir))
    throw std::runtime_error{"Path given in --datadir is not a directory."};

  Metrics metrics;

  // Construct header chain from blocks file
  data::Timechain timechain;
  LoadHeaders(options, &metrics, &timechain);

  const auto on_complete = [](const std::shared_ptr<const protocol::Block>&, int height,
                              consensus::Result) {
    if (height % 100'000 == 0) LogInfo() << height << " blocks validated...";
  };

  data::utxo::Database database{options.data_dir};
  node::sync::ValidationPipeline pipeline{timechain, database, on_complete, 16};
  metrics.Add(Op_Total, [&] {
    LogDebug() << "Reading blocks [" << 1 << ", " << options.length << ")...";
    // ChainReader reader{options.data_dir};
    CoreReader reader{options.blocks_dir};
    const auto headers = timechain.ReadHeaders();
    std::shared_ptr<const protocol::Block> block;
    std::unordered_map<protocol::Hash, std::shared_ptr<const protocol::Block>> cache;
    reader >> block;  // Read genesis.
    int height = 1;
    while (height < headers->ChainLength()) {
      if (is_abort) {
        pipeline.Abort();
        break;
      }

      metrics.Add(Op_Blocks, [&] {
        const auto& prev_hash = headers->GetChainHash(height - 1);
        // Check if required block is in the cache.
        if (const auto it = cache.find(prev_hash); it != cache.end()) {
          block = std::move(it->second);
          cache.erase(it);
        } else {  // Otherwise read a block from file.
          reader >> block;
          if (!block) util::ThrowRuntimeError("Failed to load block from file, height ", height);
          // If it's not the next block we need, put in in the cache instead.
          if (block->Header().GetPreviousBlockHash() != prev_hash)
            cache.insert(std::pair{block->Header().GetPreviousBlockHash(), std::move(block)});
        }
      });

      // If we found a block to add on this turn, add it now.
      metrics.Add(Op_Submit, [&] {
        if (block != nullptr) pipeline.Submit(std::move(block), height++);
      });
    }
    metrics.Add(Op_Wait, [&] { pipeline.Wait(); });
  });

  if (!is_abort) PrintMetrics(metrics, timechain.ReadHeaders()->ChainLength(), pipeline);

  // Tear down connection to external listeners and restore logs to the console before exiting the
  // process.
  util::SetNotificationSink(&util::DefaultLogSink::Log);
  tcp_sink.reset();

  std::cout << "\n\nThank you for running " << parser.Name() << " version " << parser.Version()
            << "!\n\n\n";
  return 0;
}

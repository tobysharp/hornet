// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include <iostream>
#include <csignal>
#include <filesystem>
#include <memory>
#include <optional>
#include <thread>
#include <vector>
#include <chrono>

#include "hornetlib/data/block_io.h"
#include "hornetlib/data/timechain.h"
#include "hornetlib/protocol/block.h"
#include "hornetnodelib/controller.h"
#include "hornetnodelib/net/constants.h"
#include "hornetnodelib/net/tcp_notification_sink.h"
#include "hornetnodelib/sync/validation_pipeline.h"
#include "hornetnodelib/util/command_line_parser.h"
#include "options.h"

using namespace hornet;

static std::atomic<bool> is_abort{false};
static std::unique_ptr<node::net::TcpNotificationSink> tcp_sink;

void HandleSignal(int) {
  is_abort = true;
  std::cout << "Aborting" << std::endl;
}

class ChainReader {
 public:
  ChainReader(const std::filesystem::path& folder) : dir_{folder} {
    if (!AdvanceReader())
      throw std::runtime_error{"Path given is not a valid directory containing a block file."};
  }

  explicit operator bool() const {
    return reader_.has_value();
  }

  ChainReader& operator >>(std::shared_ptr<const protocol::Block>& block) {
    protocol::Block b;
    *reader_ >> b;
    block = std::make_shared<const protocol::Block>(std::move(b));
    if (reader_->IsEof()) AdvanceReader();
    return *this;
  }

 private:
  bool AdvanceReader() {
    std::ostringstream oss;
    oss << "blocks" << std::setfill('0') << std::setw(4) << next_ << ".bin";
    const auto path = dir_ / oss.str();
    if (!std::filesystem::exists(path)) {
      reader_.reset();
      return false;
    }
    reader_.emplace(path);
    ++next_;
    return true;
  }

  std::filesystem::path dir_;
  std::optional<data::BlockReader> reader_;
  int next_ = 0;
};

int main(int argc, char** argv) {
  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);

  Options options;
  node::util::CommandLineParser parser("Hornet Validate Tool", "0.0.1");
  parser.AddOption("notifytcp", &options.notify_tcp_port, "Send notifications over TCP to the specified port");
  parser.AddOption("datadir", &options.data_dir, "Directory to store block files");
  parser.AddOption("length", &options.length, "Number of blocks to validate", 1'000'000);

  if (!parser.Parse(argc, argv))
    return 1;

  if (options.notify_tcp_port > 0) {
    try {
      tcp_sink = std::make_unique<node::net::TcpNotificationSink>(node::net::kLocalhost, options.notify_tcp_port);
      util::SetNotificationSink([](auto x) { return (*tcp_sink)(x); });
    } catch (const std::exception& e) {
      std::cerr << "Could not connect the TCP notification sink to port " << options.notify_tcp_port << ":" << std::endl;
      std::cerr << e.what() << std::endl;
      return -1;
    }
  }

  data::Timechain timechain;
  std::vector<std::shared_ptr<const protocol::Block>> blocks;

  // Construct header chain from blocks file
  {
    ChainReader reader{options.data_dir};
    auto headers = timechain.WriteHeaders();
    for (int height = 1; reader && height <= options.length; ++height) {
      std::shared_ptr<const protocol::Block> block;
      reader >> block;
      const auto tip = headers->ChainTip();
      timechain.AddHeader(tip, tip->Extend(block->Header()));
      blocks.emplace_back(std::move(block));
    }
  }

  data::utxo::Database database{options.data_dir};
  node::sync::ValidationPipeline pipeline{timechain, database, {}, 8};
  
  auto start_time = std::chrono::steady_clock::now();
  for (int height = 1; !is_abort && height <= options.length; ++height)
    pipeline.Submit(blocks[height - 1], height);
  pipeline.Wait(util::Timeout::Infinite());
  auto end_time = std::chrono::steady_clock::now();

  std::chrono::duration<double> elapsed = end_time - start_time;
  std::cout << "Validation took " << elapsed.count() << "s (" 
            << (options.length / elapsed.count()) << " blocks/s)" << std::endl;

  auto [val_time, val_calls] = pipeline.GetValidationMetrics();
  auto spend_metrics = pipeline.GetSpendMetrics();

  std::cout << "Metrics:" << std::endl;
  std::cout << "  Validation: " << (val_time / 1e9) << "s total (" 
            << (val_calls > 0 ? val_time / val_calls : 0) << " ns/call)" << std::endl;
  std::cout << "  Spend:      " << (spend_metrics.total_advance_time_ns / 1e9) << "s total (" 
            << (spend_metrics.total_advance_calls > 0 ? spend_metrics.total_advance_time_ns / spend_metrics.total_advance_calls : 0) << " ns/call)" << std::endl;
  std::cout << "    Parse:    " << (spend_metrics.parse_time_ns / 1e9) << "s" << std::endl;
  std::cout << "    Append:   " << (spend_metrics.append_time_ns / 1e9) << "s" << std::endl;
  std::cout << "    Query:    " << (spend_metrics.query_time_ns / 1e9) << "s" << std::endl;
  std::cout << "    Fetch:    " << (spend_metrics.fetch_time_ns / 1e9) << "s" << std::endl;

  // Tear down connection to external listeners and restore logs to the console before exiting the process.
  util::SetNotificationSink(&util::DefaultLogSink::Log);
  tcp_sink.reset();

  std::cout << "\n\nThank you for running " << parser.Name() << " version " << parser.Version() << "!\n\n\n";
  return 0;
}

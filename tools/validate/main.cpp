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
#include "hornetlib/data/block_io.h"
#include "hornetlib/data/timechain.h"
#include "hornetlib/protocol/block.h"
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

class ChainReader {
 public:
  ChainReader(const std::filesystem::path& folder) : dir_{folder} {
    if (!AdvanceReader())
      throw std::runtime_error{"Path given is not a valid directory containing a block file."};
  }

  explicit operator bool() const { return reader_.has_value(); }

  ChainReader& operator>>(std::shared_ptr<const protocol::Block>& block) {
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

template <bool kHeadersOnly = false>
class CoreReader {
 public:
  CoreReader(const std::filesystem::path& folder) : dir_{folder} {
    ReadKey();
    if (!AdvanceReader())
      throw std::runtime_error{"Path given is not a valid directory containing a block file."};
  }

  explicit operator bool() const { return reader_.has_value(); }

  using DataType = std::conditional_t<kHeadersOnly, protocol::BlockHeader,
                                      std::shared_ptr<const protocol::Block>>;

  CoreReader& operator>>(DataType& value) {
    value = reader_->ReadNext();
    if (reader_->IsEof()) AdvanceReader();
    return *this;
  }

 private:
  void ReadKey() {
    const auto path = dir_ / "xor.dat";
    if (!std::filesystem::exists(path)) util::ThrowRuntimeError("File xor.dat is missing.");
    std::ifstream stream(path, std::ios::binary);
    stream.read(reinterpret_cast<char*>(xor_key_.data()), sizeof(xor_key_));
  }

  bool AdvanceReader() {
    std::ostringstream oss;
    oss << "blk" << std::setfill('0') << std::setw(5) << next_ << ".dat";
    const auto path = dir_ / oss.str();
    if (!std::filesystem::exists(path)) {
      reader_.reset();
      return false;
    }
    reader_.emplace(path, xor_key_);
    ++next_;
    return true;
  }

  using Reader =
      std::conditional_t<kHeadersOnly, data::SerializedHeaderReader, data::SerializedBlockReader>;

  std::filesystem::path dir_;
  std::optional<Reader> reader_;
  int next_ = 0;
  std::array<uint8_t, 8> xor_key_;
};

class Timer {
 public:
  std::chrono::duration<double>& Add(auto&& func) {
    const auto start = std::chrono::high_resolution_clock::now();
    const auto start_cpu = GetCpuTime();
    func();
    accumulator_ += std::chrono::high_resolution_clock::now() - start;
    cpu_accumulator_ += GetCpuTime() - start_cpu;
    return accumulator_;
  }

  const std::chrono::duration<double>& Seconds() const { return accumulator_; }
  const std::chrono::duration<double>& CpuSeconds() const { return cpu_accumulator_; }

 private:
  static std::chrono::duration<double> GetCpuTime() {
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0) {
      return std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec);
    }
    return {};
  }

  std::chrono::duration<double> accumulator_ = {};
  std::chrono::duration<double> cpu_accumulator_ = {};
};

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
    
  Timer timer_headers, timer_read_blocks, timer_submit, timer_wait, timer_validation;

  // Construct header chain from blocks file
  data::Timechain timechain;
  timer_headers.Add([&] {
    LogDebug() << "Reading headers [" << 1 << ", " << options.length << ")...";
    // ChainReader reader{options.data_dir};
    CoreReader<true> reader{options.blocks_dir};
    std::unordered_map<protocol::Hash, const protocol::BlockHeader> cache;
    auto headers = timechain.WriteHeaders();
    std::optional<protocol::BlockHeader> header = std::make_optional<protocol::BlockHeader>();
    reader >> *header;  // Read genesis.
    while (headers->ChainLength() <= options.length) {
      const auto tip = headers->ChainTip();
      // Check if required block is in the cache.
      if (const auto it = cache.find(tip->hash); it != cache.end()) {
        header.emplace(it->second);
        cache.erase(it);
      } else if (reader) {  // Otherwise read a block from file.
        header.emplace();
        reader >> *header;
        // If it's not the next block we need, put in in the cache instead.
        if (header->GetPreviousBlockHash() != tip->hash) {
          cache.insert(std::pair{header->GetPreviousBlockHash(), *header});
          header.reset();
        }
      } else {
        break;
      }
      // If we found a block to add on this turn, add it now.
      if (header) timechain.AddHeader(tip, tip->Extend(*header));
    }
  });

  const auto on_complete = [](const std::shared_ptr<const protocol::Block>&, int height, consensus::Result) {
    if (height % 100'000 == 0)
      LogInfo() << height << " blocks validated...";
  };

  data::utxo::Database database{options.data_dir};
  node::sync::ValidationPipeline pipeline{timechain, database, on_complete, 16};
  timer_validation.Add([&] {
    LogDebug() << "Reading blocks [" << 1 << ", " << options.length << ")...";
    // ChainReader reader{options.data_dir};
    CoreReader reader{options.blocks_dir};
    const auto headers = timechain.ReadHeaders();
    std::shared_ptr<const protocol::Block> block;
    std::unordered_map<protocol::Hash, std::shared_ptr<const protocol::Block>> cache;
    reader >> block;  // Read genesis.
    int height = 1;
    while (height < headers->ChainLength()) {
      bool is_break = false;
      timer_read_blocks.Add([&] {
        const auto& prev_hash = headers->GetChainHash(height - 1);
        // Check if required block is in the cache.
        if (const auto it = cache.find(prev_hash); it != cache.end()) {
          block = std::move(it->second);
          cache.erase(it);
        } else if (reader) {  // Otherwise read a block from file.
          reader >> block;
          // If it's not the next block we need, put in in the cache instead.
          if (block->Header().GetPreviousBlockHash() != prev_hash)
            cache.insert(std::pair{block->Header().GetPreviousBlockHash(), std::move(block)});
        } else {
          is_break = true;
        }
      });
      if (is_break) break;

      // If we found a block to add on this turn, add it now.
      timer_submit.Add([&] {
        if (block != nullptr) pipeline.Submit(std::move(block), height++);
      });
    }
    timer_wait.Add([&] { pipeline.Wait(util::Timeout::Infinite()); });
  });

  std::cout << "Total time " << timer_validation.Seconds() << " ("
            << (options.length / timer_validation.Seconds().count()) << " blocks/s)" << std::endl;
  std::cout << "including " << "\n\t" << timer_read_blocks.Seconds() << " reading block files (CPU: " << timer_read_blocks.CpuSeconds() << "), "
            << "\n\t" << timer_submit.Seconds() << " submitting to the validation pipeline (CPU: " << timer_submit.CpuSeconds() << "), and " 
            << "\n\t" << timer_wait.Seconds() << " waiting for completion." << std::endl << std::endl;

  auto [val_time, val_calls] = pipeline.GetValidationMetrics();
  auto spend_metrics = pipeline.GetSpendMetrics();

  std::cout << "Metrics:" << std::endl;
  std::cout << "  Validation: " << (val_time / 1e9) << "s total ("
            << (val_calls > 0 ? val_time / val_calls : 0) << " ns/call)" << std::endl;
  std::cout << "  Spend:      " << (spend_metrics.total_advance_time_ns / 1e9) << "s total ("
            << (spend_metrics.total_advance_calls > 0
                    ? spend_metrics.total_advance_time_ns / spend_metrics.total_advance_calls
                    : 0)
            << " ns/call)" << std::endl;
  std::cout << "    Parse:    " << (spend_metrics.parse_time_ns / 1e9) << "s" << std::endl;
  std::cout << "    Append:   " << (spend_metrics.append_time_ns / 1e9) << "s" << std::endl;
  std::cout << "    Query:    " << (spend_metrics.query_time_ns / 1e9) << "s" << std::endl;
  std::cout << "    Fetch:    " << (spend_metrics.fetch_time_ns / 1e9) << "s" << std::endl;

  // Tear down connection to external listeners and restore logs to the console before exiting the
  // process.
  util::SetNotificationSink(&util::DefaultLogSink::Log);
  tcp_sink.reset();

  std::cout << "\n\nThank you for running " << parser.Name() << " version " << parser.Version()
            << "!\n\n\n";
  return 0;
}

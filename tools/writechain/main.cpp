// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <csignal>
#include <memory>
#include <optional>
#include <sstream>

#include "hornetlib/data/block_io.h"
#include "hornetnodelib/controller.h"
#include "hornetnodelib/net/constants.h"
#include "hornetnodelib/net/tcp_notification_sink.h"
#include "hornetnodelib/util/command_line_parser.h"
#include "options.h"

using namespace hornet::node;

static std::atomic<bool> is_abort{false};
static std::unique_ptr<net::TcpNotificationSink> tcp_sink;

void HandleSignal(int) {
  is_abort = true;
  std::cout << "Aborting" << std::endl;
}

class ChainWriter {
 public:
  ChainWriter(const std::filesystem::path& folder) : dir_{folder} {
    namespace fs = std::filesystem;
    fs::remove_all(dir_);
    fs::create_directories(dir_);
    if (!fs::is_directory(dir_) || !fs::is_empty(dir_))
      throw std::runtime_error{"Path given is not a directory."};
    AdvanceWriter();
  }

  ChainWriter& operator <<(const hornet::protocol::Block& block) {
    constexpr int64_t kFileSizeForRotate = 1llu << 30;
    if (writer_->SizeBytes() >= kFileSizeForRotate) 
      AdvanceWriter();
    *writer_ << block;
    ++length_;
    return *this;
  }

  int Length() const { return length_; }

 private:
  void AdvanceWriter() {
    std::ostringstream oss;
    oss << "blocks" << std::setfill('0') << std::setw(4) << next_ << ".bin";
    const auto path = dir_ / oss.str();
    writer_.emplace(path);
    ++next_;
  }

  std::filesystem::path dir_;
  std::optional<hornet::data::BlockWriter> writer_;
  int next_ = 0;
  int length_ = 0;
};

int main(int argc, char** argv) {
  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);

  Options options;
  util::CommandLineParser parser("Hornet WriteChain Tool", "0.0.1");
  parser.AddOption("connect", &options.connect, "Connect to a specific peer");
  parser.AddOption("datadir", &options.data_dir, "Directory to store block files");
  parser.AddOption("length", &options.length, "Number of blocks to write before exit");

  if (!parser.Parse(argc, argv))
    return 1;

  ChainWriter writer(options.data_dir);

  {
    Controller controller;
    if (!options.connect.host.empty())
      controller.SetConnectAddress(options.connect);
    controller.SyncManager().GetEvents().on_block_validated.Subscribe(
      [&](const hornet::data::Key&, const std::shared_ptr<const hornet::protocol::Block>& block) {
        writer << *block;
      }
    );
    controller.Initialize();
    controller.Run([&]() { 
      return is_abort.load() || writer.Length() >= options.length; 
    });
  }

  std::cout << "\n\nThank you for running " << parser.Name() << " version " << parser.Version() << "!\n\n\n";
  return 0;
}

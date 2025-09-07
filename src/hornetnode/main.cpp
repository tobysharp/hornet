// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include <iostream>
#include <csignal>
#include <memory>

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

int main(int argc, char** argv) {
  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);

  Options options;
  util::CommandLineParser parser("Hornet Node", "0.0.1");
  parser.AddOption("connect", &options.connect, "Connect to a specific peer");
  parser.AddOption("notifytcp", &options.notify_tcp_port, "Send notifications over TCP to the specified port");

  if (!parser.Parse(argc, argv))
    return 1;

  {
    Controller controller;
    if (!options.connect.host.empty())
      controller.SetConnectAddress(options.connect);
    if (options.notify_tcp_port > 0) {
      try {
        tcp_sink = std::make_unique<net::TcpNotificationSink>(net::kLocalhost, options.notify_tcp_port);
        hornet::util::SetNotificationSink([](auto x) { return (*tcp_sink)(x); });
      } catch (const std::exception& e) {
        std::cerr << "Could not connect the TCP notification sink to port " << options.notify_tcp_port << ":" << std::endl;
        std::cerr << e.what() << std::endl;
        return -1;
      }
    }
    controller.Initialize();
    controller.Run([&]() { 
      return is_abort.load(); 
    });
  }

  // Tear down connection to external listeners and restore logs to the console before exiting the process.
  hornet::util::SetNotificationSink(&hornet::util::DefaultLogSink::Log);
  tcp_sink.reset();

  std::cout << "\n\nThank you for running " << parser.Name() << " version " << parser.Version() << "!\n\n\n";
  return 0;
}

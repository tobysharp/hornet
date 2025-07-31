// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include <iostream>
#include <csignal>

#include "hornetnodelib/controller.h"
#include "hornetnodelib/util/command_line_parser.h"
#include "options.h"

std::atomic<bool> is_abort{false};

void HandleSignal(int) {
  is_abort = true;
  std::cout << "Aborting" << std::endl;
}

int main(int argc, char** argv) {
  using namespace hornet::node;

  Options options;
  util::CommandLineParser parser("Hornet Node", "0.0.1");
  parser.AddOption("connect", &options.connect, "Connect to a specific peer");

  if (!parser.Parse(argc, argv))
    return 1;

  Controller controller;
  if (!options.connect.host.empty())
    controller.SetConnectAddress(options.connect);
  controller.Initialize();

  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);
  controller.Run([&]() { return is_abort.load(); });

  return 0;
}

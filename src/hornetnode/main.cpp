// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include <iostream>
#include <csignal>

#include "hornetnodelib/controller.h"

std::atomic<bool> is_abort{false};

void HandleSignal(int) {
  is_abort = true;
  std::cout << "Aborting" << std::endl;
}

int main() {
  std::cout << "Hornet Node" << std::endl;

  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);

  hornet::node::Controller controller;
  controller.Initialize();
  controller.Run([&]() { return is_abort.load(); });

  std::cout << "Exit" << std::endl;
  return 0;
}
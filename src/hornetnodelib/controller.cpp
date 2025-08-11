// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetnodelib/controller.h"

namespace hornet::node {

Controller::Controller()
    : block_validation_status_(decltype(block_validation_status_)::Create(timechain_)),
      message_loop_(peer_manager_),
      sync_manager_(timechain_, block_validation_status_) {
  message_loop_.AddEventHandler(&peer_negotiator_);
  message_loop_.AddEventHandler(&sync_manager_);
}

Controller::~Controller() {
  Stop();
}

void Controller::Initialize() {
  if (!connect_address_.host.empty())
    message_loop_.AddOutboundPeer(connect_address_.host, connect_address_.port);
}

void Controller::Start() {
  if (running_.exchange(true)) return;

  // Start the message loop in a separate thread.
  message_loop_thread_ = std::thread([this]() {
    message_loop_.RunMessageLoop();
    running_ = false;
  });
}

void Controller::Run(BreakCondition condition) {
  Assert(!running_.exchange(true));
  message_loop_.RunMessageLoop(condition);
  running_ = false;
}

void Controller::Stop() {
  // Stop the message loop and join the thread.
  message_loop_.Abort();
  if (message_loop_thread_.joinable())
    message_loop_thread_.join();
}

}  // namespace hornet::node

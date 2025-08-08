// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <atomic>
#include <thread>

#include "hornetlib/data/keyframe_sidecar.h"
#include "hornetlib/data/timechain.h"
#include "hornetnodelib/dispatch/peer_negotiator.h"
#include "hornetnodelib/dispatch/protocol_loop.h"
#include "hornetnodelib/net/peer_address.h"
#include "hornetnodelib/net/peer_manager.h"
#include "hornetnodelib/sync/block_sync.h"
#include "hornetnodelib/sync/sync_manager.h"

namespace hornet::node {

class Controller {
 public:
  using BreakCondition = dispatch::ProtocolLoop::BreakCondition;

  Controller();
  ~Controller();

  void SetConnectAddress(const net::PeerAddress& address) {
    connect_address_ = address;
  }

  // Initialize the controller, setting up necessary components.
  void Initialize();

  // Start the controller, beginning its operation.
  void Start();

  // Stop the controller, cleaning up resources.
  void Stop();

  // Run the protocol loop in the current thread, exiting when the break condition returns true.
  void Run(BreakCondition condition);

 private:
  template <typename T> using SidecarHandle = data::Timechain::SidecarHandle<T>;

  data::Timechain timechain_;  // The timechain managed by this controller.
  SidecarHandle<sync::BlockValidationStatus> block_validation_status_;  // Tracks block validation status.

  std::thread message_loop_thread_;      // Thread for processing protocol messages.
  dispatch::ProtocolLoop message_loop_;  // Handles protocol messages.
  std::atomic<bool> running_{false};

  net::PeerManager peer_manager_;             // Manages network peers.
  dispatch::PeerNegotiator peer_negotiator_;  // Negotiates peer connections.
  net::PeerAddress connect_address_;          // Address to connect to if specified.
  
  sync::SyncManager sync_manager_;  // Handles initial synchronization of the timechain with peers.
};

}  // namespace hornet::node
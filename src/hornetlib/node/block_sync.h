// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include "hornetlib/data/timechain.h"
#include "hornetlib/net/peer.h"
#include "hornetlib/node/sync_handler.h"
#include "hornetlib/protocol/message/block.h"
#include "hornetlib/protocol/message/getdata.h"
#include "hornetlib/util/thread_safe_queue.h"

namespace hornet::node {

class BlockSync {
 public:
  BlockSync(data::Timechain& timechain, SyncHandler& handler);
  ~BlockSync();

  // Sets the maximum number of bytes allowed in the queue
  void SetMaxQueueBytes(int max_queue_bytes) {
    max_queue_bytes_ = max_queue_bytes;
  }

  // Begins downloading and validating blocks from a given peer.
  void StartSync(net::WeakPeer peer);

  void OnBlock(const net::WeakPeer peer, const protocol::message::Block& message);

 protected:
  struct Item {
    net::WeakPeer peer;
    std::shared_ptr<const protocol::Block> block;
  };

  // Validates queued blocks, and adds them to the timechain.
  void Process();

  // Requests more headers via the callback supplied in RegisterPeer.
  void RequestBlockFrom(net::WeakPeer weak, const protocol::Hash& hash);

  data::Timechain& timechain_;
  SyncHandler& handler_;
  util::ThreadSafeQueue<Item> queue_;
  std::thread worker_thread_;          // Background worker thread for processing.
  std::atomic<int> queue_bytes_ = 0;   // Size in bytes of the queued items.
  int max_queue_bytes_ = 16 << 20;     // Default queue capacity to hide download latency.

  // Note that in BlockSync we don't have the send_blocked_ flag that we have in HeaderSync,
  // because this flag enforces serial requests -- for getheaders we need to wait to learn the
  // hash of the last requested header before we can request more. But for getdata messages we
  // don't have the same constraint since the hashes are already known. Therefore we may request
  // multiple blocks simultaneously, provided we can cope with the memory bandwidth. Hence we
  // don't need and don't want the send_blocked_ flag that enforces just one in-flight request.
};

inline BlockSync::BlockSync(data::Timechain& timechain, SyncHandler& handler)
      : timechain_(timechain), handler_(handler), worker_thread_([this] { this->Process(); }) {
}

inline BlockSync::~BlockSync() {
  queue_.Stop();
  worker_thread_.join();
}

inline void BlockSync::RequestBlockFrom(net::WeakPeer weak, const protocol::Hash& hash) {
  if (queue_bytes_ < max_queue_bytes_) {
    if (const auto peer = weak.lock()) {
      protocol::message::GetData getdata;
      getdata.AddInventory(protocol::Inventory::WitnessBlock(hash));
      handler_.OnRequest(peer, std::make_unique<protocol::message::GetData>(std::move(getdata)));
    }
  }
}

inline void BlockSync::StartSync(net::WeakPeer peer) {
  const protocol::Hash& hash = timechain_.Headers().HeaviestChain().GetHash(0);
  RequestBlockFrom(peer, hash);
}

inline void BlockSync::OnBlock(const net::WeakPeer peer, const protocol::message::Block& message) {
  // Pushes work onto the thread-safe async work queue.
  Item item{peer, message.GetBlock()};
  queue_bytes_ += sizeof(Item) + item.block->SizeBytes();
  queue_.Push(std::move(item));
}

inline void BlockSync::Process() {
  for (std::optional<Item> item; (item = queue_.WaitPop());) {
  }
}

}  // namespace hornet::node

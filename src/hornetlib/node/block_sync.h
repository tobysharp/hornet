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

  void OnBlock(net::SharedPeer peer, const protocol::message::Block& message);

 protected:
  struct Item {
    net::WeakPeer peer;
    int height;
    std::shared_ptr<const protocol::Block> block;
  };

  static int SizeInBytes(const Item& item) {
    return sizeof(Item) + item.block->SizeBytes();
  }

  enum class RequestState { Active, Deferred, Disconnected, End };

  // Validates queued blocks, and adds them to the timechain.
  void Process();

  // Requests more headers via the callback supplied in RegisterPeer.
  RequestState RequestNextBlock(net::WeakPeer weak);

  data::Timechain& timechain_;
  SyncHandler& handler_;
  util::ThreadSafeQueue<Item> queue_;
  std::thread worker_thread_;         // Background worker thread for processing.
  std::atomic<int> queue_bytes_ = 0;  // Size in bytes of the queued items.
  int max_queue_bytes_ = 16 << 20;    // Default queue capacity to hide download latency.

  // Note that in BlockSync we don't have the request_active_ flag that we have in HeaderSync,
  // because this flag enforces serial requests -- for getheaders we need to wait to learn the
  // hash of the last requested header before we can request more. But for getdata messages we
  // don't have the same constraint since the hashes are already known. Therefore we may request
  // multiple blocks simultaneously, provided we can cope with the memory bandwidth. Hence we
  // don't need and don't want the request_active_ flag that enforces just one in-flight request.

  std::atomic_flag request_active_;
  // EDIT: Right now, we *do* still have the request_active_ flag, because we're going to start
  // with the simplest possible logic for block sync, and incrementally add features like multiple
  // simultaneous in-flight requests.

  int request_height_ = 1;  // The chain height of the next block to request.
  // TODO: Not sure if request_height_ needs to be std::atomic. It's currently only modified inside
  // a scope that can be only accessed by one thread at a time. But other threads could be reading.
};

inline BlockSync::BlockSync(data::Timechain& timechain, SyncHandler& handler)
    : timechain_(timechain), handler_(handler), worker_thread_([this] { this->Process(); }) {}

inline BlockSync::~BlockSync() {
  queue_.Stop();
  worker_thread_.join();
}

inline BlockSync::RequestState BlockSync::RequestNextBlock(net::WeakPeer weak) {
  // Stop requesting after we fill the queue.
  if (queue_bytes_ >= max_queue_bytes_) return RequestState::Deferred;
  const auto peer = weak.lock();
  if (!peer) return RequestState::Disconnected;
  // Only send message if we have an empty request slot available.
  if (!request_active_.test_and_set(std::memory_order_acquire)) {
    // Only one thread at a time can get into this scope.
    const bool finished = request_height_ >= timechain_.Headers().GetHeaviestLength();
    if (finished) {
      request_active_.clear();
      return RequestState::End;
    }
    protocol::message::GetData getdata;
    const protocol::Hash& hash = timechain_.Headers().HeaviestChain().GetHash(request_height_++);
    getdata.AddInventory(protocol::Inventory::WitnessBlock(hash));
    handler_.OnRequest(peer, std::make_unique<protocol::message::GetData>(std::move(getdata)));
    return RequestState::Active;
  }
  return RequestState::Deferred;
}

inline void BlockSync::StartSync(net::WeakPeer peer) {
  // TODO: Here we want to inspect our timechain state to determine what validation has
  // already taken place, to update request_height_. request_height_=1 is fine for starting
  // from scratch, but we may be starting from an interrupted sync where some validation has
  // already been done. So once we figure out where we are storing that state of current block
  // validation progress against the active header timechain, we should look that up here to
  // initialize request_height_ appropriately, to continue from previous state.
  request_height_ = 1;

  request_active_.clear(std::memory_order::release);
  RequestNextBlock(peer);
}

inline void BlockSync::OnBlock(net::SharedPeer peer, const protocol::message::Block& message) {
  Assert(request_active_.test());

  // Note the block is shared rather than copied, for performance.
  const std::shared_ptr<const protocol::Block> block = message.GetBlock();
  const int received_height = request_height_ - 1;

  // Before pushing the block onto the validation queue, check the received block header against 
  // the header we requested from. If the headers don't exactly match, we already know we need 
  // to fail validation and disconnect the peer.

  // TODO: To do this safely, we have to request the header by both chain height and hash.
  // We should store the hash we requested and its height in our request tracker state. Then
  // when we receive the block, we should compute its hash, check that against our request first.
  // That allows us to match the block to the request. Then we no longer need to compare the
  // header to the one in the chain.

  // Pushes work onto the thread-safe async work queue.
  Item item{peer, received_height, block};
  queue_bytes_ += SizeInBytes(item);
  queue_.Push(std::move(item));

  // Now free up the spare request slot and consider requesting the next block.
  request_active_.clear(std::memory_order::release);
  RequestNextBlock(peer);
}

inline void BlockSync::Process() {
  for (std::optional<Item> item; (item = queue_.WaitPop());) {
    queue_bytes_ -= SizeInBytes(*item);

    // As soon as we pop from the queue, we can consider filling the empty queue slot.
    const auto request_state = RequestNextBlock(item->peer);

    // TODO: Validate the block.

    // TODO: If validation fails, disconnect/ban the peer that provided it,
    // delete the header subtree, add the header hash to a blacklist,
    // delete this block and any downstream blocks, and cancel any downstream block requests.

    // TODO: "Connect" the block, i.e. update the current UTXO set with its transactions.

    // TODO: Maybe update some field in the header timechain to indicate
    // where we are up to with block validation?

    if (request_state == RequestState::End) handler_.OnComplete(item->peer);
  }
}

}  // namespace hornet::node

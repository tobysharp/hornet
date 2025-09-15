// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <sstream>
#include <thread>

#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/validate_block.h"
#include "hornetlib/data/sidecar_binding.h"
#include "hornetlib/data/timechain.h"
#include "hornetlib/protocol/message/block.h"
#include "hornetlib/protocol/message/getdata.h"
#include "hornetlib/util/notify.h"
#include "hornetlib/util/thread_safe_queue.h"
#include "hornetlib/util/throw.h"
#include "hornetnodelib/net/peer.h"
#include "hornetnodelib/sync/sync_handler.h"
#include "hornetnodelib/sync/types.h"

namespace hornet::node::sync {

class BlockSync {
 public:
  BlockSync(data::Timechain& timechain, BlockValidationBinding validation, SyncHandler& handler);
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
    data::Key id;
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

  // Gets the next block ID to request from a peer.
  std::optional<data::Key> GetNextBlockId() const;

  consensus::BlockError ValidateItem(const Item& item);
  void HandleError(const Item& item, consensus::BlockError error);

  data::Timechain& timechain_;
  BlockValidationBinding validation_;
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

  data::Key request_;
};

inline BlockSync::BlockSync(data::Timechain& timechain, BlockValidationBinding validation,
                            SyncHandler& handler)
    : timechain_(timechain),
      validation_(validation),
      handler_(handler),
      worker_thread_([this] { this->Process(); }) {}

inline BlockSync::~BlockSync() {
  queue_.Stop();
  worker_thread_.join();
}

// Returns the next block key to request from a peer.
inline std::optional<data::Key> BlockSync::GetNextBlockId() const {
  // Takes a read lock on the timechain while we determine the next block to request.
  const auto headers = timechain_.ReadHeaders();

  // Checks whether the last requested block is still in the main chain.
  if (request_.height > 0 && request_.height < headers->ChainLength() &&
      headers->GetChainHash(request_.height) == request_.hash) {
    // The last requested block is still in the main chain, so we can simply
    // request the next block in the chain.
    if (headers->ChainLength() > request_.height + 1)
      return data::Key{request_.height + 1, headers->GetChainHash(request_.height + 1)};
    else
      return std::nullopt;
  }

  // Either there was no previous request, or the previously requested block got re-orged
  // out of the main chain. In either case, now we defer to the validation status sidecar
  // to ask it for the first unvalidated block in the chain.
  const auto unvalidated =
      validation_.Sidecar().FindInChainIf(1, [](consensus::BlockValidationStatus status) {
        return status == consensus::BlockValidationStatus::Unvalidated;
      });
  if (unvalidated)
    return data::Key{*unvalidated, headers->GetChainHash(*unvalidated)};
  else
    return std::nullopt;
}

inline BlockSync::RequestState BlockSync::RequestNextBlock(net::WeakPeer weak) {
  // Stop requesting after we fill the queue.
  if (queue_bytes_ >= max_queue_bytes_) return RequestState::Deferred;
  const auto peer = weak.lock();
  if (!peer) return RequestState::Disconnected;
  // Proceeds only if we have an empty request slot available.
  if (!request_active_.test_and_set(std::memory_order_acquire)) {
    // Only one thread at a time can get into this scope.

    // Queries the block-validation sidecar to see which block we should request next.
    std::optional<data::Key> next = GetNextBlockId();
    if (!next.has_value()) {
      request_active_.clear(std::memory_order::release);
      return RequestState::End;  // No more blocks to request.
    }

    // Saves the block key into request_ and queues the GetData message for the peer.
    request_ = *next;
    // LogDebug() << "Block height " << request_.height << " requested.";
    protocol::message::GetData getdata;
    getdata.AddInventory(protocol::Inventory::WitnessBlock(request_.hash));
    handler_.OnRequest(peer, std::make_unique<protocol::message::GetData>(std::move(getdata)));
    return RequestState::Active;
  }
  return RequestState::Deferred;
}

inline void BlockSync::StartSync(net::WeakPeer peer) {
  Assert(!request_active_.test());
  if (RequestNextBlock(peer) == RequestState::End) {
    handler_.OnComplete(peer);  // No blocks will ever reach the queue.
  }
}

inline void BlockSync::OnBlock(net::SharedPeer peer, const protocol::message::Block& message) {
  const data::Key expected = request_;
  if (!request_active_.test() || expected.height < 0) {
    LogWarn() << "Ignoring unsolicited or cancelled block from peer " << peer->GetId() << ".";
    return;
  }
  
  // Note the block is shared rather than copied, for performance.
  const std::shared_ptr<const protocol::Block> block = message.GetBlock();

  // Before pushing the block onto the validation queue, check the received block header against
  // the header we requested from. If the headers don't have the same hash, we already know we need
  // to fail validation and disconnect the peer.
  if (block->Header().ComputeHash() != expected.hash) {
    // If the block's hash does not match the requested hash, we have a protocol violation.
    handler_.OnError(peer, "Received block hash does not match requested hash.");
    return;
  }

  // Pushes work onto the thread-safe async work queue.
  Item item{peer, expected, block};
  queue_bytes_ += SizeInBytes(item);
  queue_.Push(std::move(item));

  // Now we have queued the block, free up one request slot for another download.
  request_active_.clear(std::memory_order::release);

  // Consider requesting the next block immediately, if we have space in the queue.
  RequestNextBlock(peer);
}

inline consensus::BlockError BlockSync::ValidateItem(const Item& item) {
  // Validates the block.
  consensus::BlockError error = consensus::ValidateBlockStructure(*item.block);
  if (error == consensus::BlockError::None) {
    // Lock the header chain during the scope of contextual validation.
    const auto headers = timechain_.ReadHeaders();
    // Find the header for this block, and advance up the tree to its parent.
    const auto header = headers->FindStable(item.id.height, item.id.hash);
    if (!header) {
      // The block we requested and downloaded and queued is bizarrely now not found in our header timechain.
      // This should be completely impossible, especially since headers are append-only.
      util::ThrowLogicError("Header not found during block sync height ", item.id.height, ".");
    }
    const auto parent = std::next(header);
    // Create a validation view with the parent as the tip.
    const auto view = headers->GetValidationView(parent);
    // Call the contextual block validation.
    error = consensus::ValidateBlockContext(*view, *item.block);
  }
  return error;
}

inline void BlockSync::Process() {
  for (std::optional<Item> item; (item = queue_.WaitPop());) {
    queue_bytes_ -= SizeInBytes(*item);

    // As soon as we pop from the queue, we can consider filling the empty queue slot.
    const auto request_state = RequestNextBlock(item->peer);

    // Validates the block.
    consensus::BlockError error = ValidateItem(*item);

    // If validation fails, disconnect/ban the peer that provided it,
    // delete this block and any downstream blocks, and cancel any downstream block requests.
    if (error != consensus::BlockError::None) {
      HandleError(*item, error);
      continue;
    }

    // Sets the validation status flag into the metadata sidecar.
    util::NotifyMetric("sync/blocks", {{"blocks_validated", item->id.height + 1}});
    LogDebug() << "Block height " << item->id.height << " validated, " << item->block->SizeBytes()
               << " bytes.";
    validation_.Set(item->id, consensus::BlockValidationStatus::StructureValid);

    // TODO: Update the current UTXO set and the active chain tip, once all necessary validation is
    // complete. We might choose to do this in a separate thread for increased parallelism.

    // TODO: According to the active policy, store this block to disk, or move it to the block
    // cache, or just let it vanish after we're done with validation.

    if (request_state == RequestState::End) handler_.OnComplete(item->peer);
  }
}

inline void BlockSync::HandleError(const Item& item, consensus::BlockError error) {
  std::ostringstream oss;
  oss << "Block validation error code " << static_cast<int>(error) << ".";

  // Drops peer immediately, and potentially applies misbehavior penalties.
  handler_.OnError(item.peer, oss.str());

  // Removes any queued blocks from the same peer.
  queue_.EraseIf([&](const Item& queued) { return item.peer == queued.peer; });

  // Deletes any in-flight block download requests pertaining to this peer.
  request_active_.clear();
  request_ = {};

  // In a design where blocks are downloaded ahead of validation, we would need to
  // track which blocks came from which peer, and delete downstream blocks from
  // misbehaving peers. Since download and validation are currently coupled, this is not needed.
}

}  // namespace hornet::node::sync

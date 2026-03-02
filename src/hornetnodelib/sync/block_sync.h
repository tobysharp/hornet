// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <atomic>
#include <cstdint>
#include <format>
#include <memory>
#include <thread>
#include <variant>

#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/validate_api.h"
#include "hornetlib/data/key.h"
#include "hornetlib/data/sidecar_binding.h"
#include "hornetlib/data/timechain.h"
#include "hornetlib/data/utxo/database.h"
#include "hornetlib/protocol/message/block.h"
#include "hornetlib/protocol/message/getdata.h"
#include "hornetlib/util/notify.h"
#include "hornetlib/util/thread_safe_queue.h"
#include "hornetlib/util/throw.h"
#include "hornetnodelib/net/peer.h"
#include "hornetnodelib/sync/sync_handler.h"
#include "hornetnodelib/sync/types.h"
#include "hornetnodelib/sync/validation_pipeline.h"

namespace hornet::node::sync {

class BlockSyncHandler : public SyncHandler {
 public:
  virtual void OnBlockValidated(net::WeakPeer peer, const data::Key& key,
                                const std::shared_ptr<const protocol::Block>& block) = 0;
};

class BlockSync {
 public:
  BlockSync(data::Timechain& timechain, data::utxo::Database& database,
            BlockValidationBinding validation, BlockSyncHandler& handler);
  ~BlockSync();

  // Sets the maximum number of bytes allowed in the queue
  void SetMaxQueueBytes(int max_queue_bytes) { max_queue_bytes_ = max_queue_bytes; }

  // Begins downloading and validating blocks from a given peer.
  void StartSync(net::WeakPeer peer);

  void OnBlock(net::SharedPeer peer, const protocol::message::Block& message);

 protected:
  struct Item {
    net::WeakPeer peer;
    data::Key id;
    std::shared_ptr<const protocol::Block> block;
  };

  struct CompletionState {
    net::WeakPeer peer;
    bool is_final;
  };

  static int SizeInBytes(const Item& item) { return sizeof(Item) + item.block->SizeBytes(); }

  enum class RequestState { Active, Deferred, Disconnected, End };

  // Validates queued blocks, and adds them to the timechain.
  void Process();

  // Requests more headers via the callback supplied in RegisterPeer.
  RequestState RequestNextBlock(net::WeakPeer weak);

  // Gets the next block ID to request from a peer.
  std::optional<data::Key> GetNextBlockId() const;

  consensus::Result ValidateItem(const Item& item);
  void HandleError(const net::WeakPeer& peer, consensus::Error error);

  void OnValidateComplete(const std::shared_ptr<const protocol::Block>& block, const data::Key& id,
                          consensus::Result result, const CompletionState& state);

  data::Timechain& timechain_;
  BlockValidationBinding validation_;
  BlockSyncHandler& handler_;
  ValidationPipeline pipeline_;
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

inline BlockSync::BlockSync(data::Timechain& timechain, data::utxo::Database& database,
                            BlockValidationBinding validation, BlockSyncHandler& handler)
    : timechain_(timechain),
      validation_(validation),
      handler_(handler),
      pipeline_(timechain, database),
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
    else return std::nullopt;
  }

  // Either there was no previous request, or the previously requested block got re-orged
  // out of the main chain. In either case, now we defer to the validation status sidecar
  // to ask it for the first unvalidated block in the chain.
  const auto unvalidated = validation_.Sidecar().FindInChainIf(
      1, [](BlockValidationStatus status) { return status == BlockValidationStatus::Unvalidated; });
  if (unvalidated) return data::Key{*unvalidated, headers->GetChainHash(*unvalidated)};
  else return std::nullopt;
}

inline BlockSync::RequestState BlockSync::RequestNextBlock(net::WeakPeer weak) {
  // Stop requesting after we fill the queue.
  if (queue_bytes_ >= max_queue_bytes_) return RequestState::Deferred;
  const auto peer = weak.lock();
  if (!peer) return RequestState::Disconnected;
  // Proceeds only if we have an empty request slot available.
  if (!request_active_.test_and_set(std::memory_order::acquire)) {
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

inline void BlockSync::Process() {
  for (std::optional<Item> item; (item = queue_.WaitPop());) {
    queue_bytes_ -= SizeInBytes(*item);

    // As soon as we pop from the queue, we can consider filling the empty queue slot.
    const auto request_state = RequestNextBlock(item->peer);

    CompletionState completion_state{item->peer, request_state == RequestState::End};
    pipeline_.Submit(
        item->block, item->id.height, /*assume_valid =*/true,
        [this, state = std::move(completion_state)](
            const std::shared_ptr<const protocol::Block>& block, const data::Key& id,
            consensus::Result result) { OnValidateComplete(block, id, result, state); });
  }
}

inline void BlockSync::OnValidateComplete(const std::shared_ptr<const protocol::Block>& block,
                                          const data::Key& id, consensus::Result result,
                                          const CompletionState& state) {
  // If validation fails, disconnect/ban the peer that provided it,
  // delete this block and any downstream blocks, and cancel any downstream block requests.
  if (!result) {
    HandleError(state.peer, result.Error());
    return;
  }

  // Sets the validation status flag into the metadata sidecar.
  util::NotifyMetric("sync/blocks", {{"blocks_validated", id.height + 1}});
  LogDebug() << "Block height " << id.height << " validated, " << block->SizeBytes() << " bytes.";
  validation_.Set(id, BlockValidationStatus::StructureValid);

  handler_.OnBlockValidated(state.peer, id, block);

  // TODO: Update the current UTXO set and the active chain tip, once all necessary validation is
  // complete. We might choose to do this in a separate thread for increased parallelism.

  // TODO: According to the active policy, store this block to disk, or move it to the block
  // cache, or just let it vanish after we're done with validation.

  if (state.is_final) handler_.OnComplete(state.peer);
}

inline void BlockSync::HandleError(const net::WeakPeer& peer, consensus::Error error) {
  const auto msg = std::format("Validation error code {}.", static_cast<int>(error));

  // Drops peer immediately, and potentially applies misbehavior penalties.
  handler_.OnError(peer, msg);

  // Removes any queued blocks from the same peer.
  queue_.EraseIf([&](const Item& queued) { return peer == queued.peer; });

  // Deletes any in-flight block download requests pertaining to this peer.
  request_active_.clear();
  request_ = {};

  // In a design where blocks are downloaded ahead of validation, we would need to
  // track which blocks came from which peer, and delete downstream blocks from
  // misbehaving peers. Since download and validation are currently coupled, this is not needed.
}

}  // namespace hornet::node::sync

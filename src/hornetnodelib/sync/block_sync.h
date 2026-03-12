// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
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

  // Begins downloading and validating blocks from a given peer.
  void StartSync(net::WeakPeer peer);

  void OnBlock(net::SharedPeer peer, const protocol::message::Block& message);

 protected:
  struct Item {
    net::WeakPeer peer;
    data::Key id;
    std::shared_ptr<const protocol::Block> block;
  };

  struct Counters {
    struct Counter {
      std::atomic<int64_t> live = 0;
      int64_t snapshot = 0;
      void operator++() { ++live; }
      void operator--() { --live; }
      void Snap() { snapshot = live; }
      int64_t Delta() const { return live - snapshot; }
      operator int64_t() const { return live; }
    };
    Counter requested, pending, received, submitted, validated, flush_due_ns;
    void OnRequest() { ++requested; ++pending; }
    void OnReceive() { --pending; ++received; }
    void OnValidate() { ++validated; }
    void OnSubmit() { ++submitted; }
    void Snapshot() { for (auto* x : {&requested, &pending, &received, &submitted, &validated}) x->Snap(); }
    int64_t TickNs() {
      using namespace std::chrono_literals;
      static constexpr int64_t kFlushPeriodNs = std::chrono::duration_cast<std::chrono::nanoseconds>(1s).count();
      const int64_t now = NowNs();            // The current wall-clock time.
      int64_t due = flush_due_ns.live;  // The time that we are due for another flush.
      if (now < due) return 0;
      if (!flush_due_ns.live.compare_exchange_strong(due, now + kFlushPeriodNs)) return 0;
      const int64_t prev = flush_due_ns.snapshot;  // The time when we made the last flush.
      flush_due_ns.snapshot = now;                 // Update the time of the "last" flush to the current time.
      if (prev == 0) {
        Snapshot();
        return 0;
      }
      return now - prev;
    }
    static int64_t NowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
  };

  static int SizeInBytes(const Item& item) { return sizeof(Item) + item.block->SizeBytes(); }

  enum class RequestState { Active, Deferred, Disconnected, End };

  // Dequeues blocks and submits them to the validation pipeline.
  void Process();

  // Requests more blocks from the peer via handler_.OnRequest.
  RequestState RequestNextBlocks(net::WeakPeer weak);

  // Gets the next block ID to request from a peer.
  std::optional<data::Key> GetNextBlockId(const data::HeaderTimechain& headers) const;

  consensus::Result ValidateItem(const Item& item);
  void HandleError(const net::WeakPeer& peer, consensus::Error error);

  void OnValidateComplete(const std::shared_ptr<const protocol::Block>& block, const data::Key& id,
                          consensus::Result result, const net::WeakPeer& weak);

  void MaybeFlush() const;

  data::Timechain& timechain_;
  BlockValidationBinding validation_;
  BlockSyncHandler& handler_;
  ValidationPipeline pipeline_;
  util::ThreadSafeQueue<Item> queue_;
  std::thread worker_thread_;         // Background worker thread for processing.
  std::atomic<int> queue_bytes_ = 0;  // Size in bytes of the queued items.
  mutable std::mutex request_mutex_;  // Protects the variables below.
  data::Key last_request_;            // The id of the last block that was requested.
  std::vector<data::Key> pending_;    // The ids of all the pending block requests.
  int next_request_height_ = 0;       // The height of the next block to be requested.
  mutable Counters perf_;

  static constexpr int kMaxInFlight = 128;  // Max number of inflight block requests.
  static constexpr int kMaxBlockBytes = 4 << 20;
  static constexpr int kMaxBufferedBytes = kMaxInFlight * kMaxBlockBytes * 2;
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
inline std::optional<data::Key> BlockSync::GetNextBlockId(
    const data::HeaderTimechain& headers) const {
  // Checks whether the last requested block is still in the main chain. If so, that implies
  // there has not been a reorg above us in the chain since the last request.
  if (last_request_.height > 0 && last_request_.height < headers.ChainLength() &&
      headers.GetChainHash(last_request_.height) == last_request_.hash) {
    // The last requested block is still in the main chain, so we can simply
    // request the next block in the chain.
    if (headers.ChainLength() > last_request_.height + 1)
      return data::Key{last_request_.height + 1, headers.GetChainHash(last_request_.height + 1)};
    else return std::nullopt;
  }

  // Either there was no previous request, or the previously requested block got re-orged
  // out of the main chain. In either case, now we defer to the validation status sidecar
  // to ask it for the first unvalidated block in the chain.
  const auto unvalidated = validation_.Sidecar().FindInChainIf(
      1, [](BlockValidationStatus status) { return status == BlockValidationStatus::Unvalidated; });
  if (unvalidated) return data::Key{*unvalidated, headers.GetChainHash(*unvalidated)};
  else return std::nullopt;
}

inline BlockSync::RequestState BlockSync::RequestNextBlocks(net::WeakPeer weak) {
  const auto peer = weak.lock();
  if (!peer) return RequestState::Disconnected;  // Peer has been disconnected.

  // After we take this scoped lock on the header timechain, we can be sure that we won't experience
  // a reorg of the chain while we're holding the lock.
  const auto headers = timechain_.ReadHeaders();
  std::lock_guard lock(request_mutex_);
  // First we determine the next unvalidated block to be requested.
  const auto next = GetNextBlockId(headers);
  if (!next) return RequestState::End;  // No more blocks to request.

  next_request_height_ = next->height;
  bool requested = false;
  while (next_request_height_ >= 1 && next_request_height_ < headers->ChainLength()) {
    const int buffered_bytes = queue_bytes_ + (std::ssize(pending_) + 1) * kMaxBlockBytes;
    if (std::ssize(pending_) >= kMaxInFlight || buffered_bytes >= kMaxBufferedBytes)
      return requested ? RequestState::Active : RequestState::Deferred;

    const data::Key key{next_request_height_, headers->GetChainHash(next_request_height_)};
    protocol::message::GetData getdata;
    getdata.AddInventory(protocol::Inventory::WitnessBlock(key.hash));
    handler_.OnRequest(weak, std::make_unique<protocol::message::GetData>(std::move(getdata)));
    perf_.OnRequest();
    pending_.push_back(key);
    ++next_request_height_;
    last_request_ = key;
    requested = true;
  }
  return requested ? RequestState::Active : RequestState::End;
}

inline void BlockSync::StartSync(net::WeakPeer peer) {
  {
    std::lock_guard lock(request_mutex_);
    Assert(pending_.empty());
  }
  std::construct_at(&perf_);
  if (RequestNextBlocks(peer) == RequestState::End) {
    handler_.OnComplete(peer);  // No blocks will ever reach the queue.
  }
}

inline void BlockSync::OnBlock(net::SharedPeer peer, const protocol::message::Block& message) {
  // Note the block is shared rather than copied, for performance.
  const std::shared_ptr<const protocol::Block> block = message.GetBlock();

  data::Key received;
  {
    std::lock_guard lock(request_mutex_);

    if (pending_.empty() || last_request_.height < 0) {
      LogWarn() << "Ignoring unsolicited or cancelled block from peer " << peer->GetId() << ".";
      return;
    }

    // Match the received block hash against outstanding requested block hashes.
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
      if (it->hash == block->Header().ComputeHash()) {
        received = *it;
        *it = pending_.back();
        pending_.pop_back();
        break;
      }
    }
    // If no match is found, treat it as a protocol violation.
    if (received.height < 0) {
      handler_.OnError(peer, "Received block hash does not match any requested hash.");
      return;
    }
  }

  // Pushes work onto the thread-safe async work queue.
  Item item{peer, received, block};
  queue_bytes_ += SizeInBytes(item);
  queue_.Push(std::move(item));
  perf_.OnReceive();

  // Consider requesting the next block immediately, if we have space in the queue.
  RequestNextBlocks(peer);
  MaybeFlush();
}

inline void BlockSync::Process() {
  for (std::optional<Item> item; (item = queue_.WaitPop());) {
    queue_bytes_ -= SizeInBytes(*item);

    // As soon as we pop from the queue, we can consider filling the empty queue slot.
    RequestNextBlocks(item->peer);
    perf_.OnSubmit();
    pipeline_.Submit(
        item->block, item->id.height, /*assume_valid =*/true,
        [this, weak = std::move(item->peer)](const std::shared_ptr<const protocol::Block>& block,
                                             const data::Key& id, consensus::Result result) {
          OnValidateComplete(block, id, result, weak);
        });
    MaybeFlush();
  }
}

inline void BlockSync::OnValidateComplete(const std::shared_ptr<const protocol::Block>& block,
                                          const data::Key& id, consensus::Result result,
                                          const net::WeakPeer& weak) {
  // If validation fails, disconnect/ban the peer that provided it,
  // delete this block and any downstream blocks, and cancel any downstream block requests.
  if (!result) {
    HandleError(weak, result.Error());
    return;
  }

  // Sets the validation status flag into the metadata sidecar.
  util::NotifyMetric("sync/blocks", {{"blocks_validated", id.height + 1}});
  LogDebug() << "Block height " << id.height << " validated, " << block->SizeBytes() << " bytes.";
  validation_.Set(id, BlockValidationStatus::StructureValid);

  handler_.OnBlockValidated(weak, id, block);
  perf_.OnValidate();

  // TODO: Update the current UTXO set and the active chain tip, once all necessary validation is
  // complete. We might choose to do this in a separate thread for increased parallelism.

  // TODO: According to the active policy, store this block to disk, or move it to the block
  // cache, or just let it vanish after we're done with validation.

  // When we have validated the last block in the main chain, signal completion.
  // TODO: Consider whether we want to validate blocks on other forks too.
  const auto headers = timechain_.ReadHeaders();
  if (headers->ChainLength() == id.height + 1 && headers->GetChainHash(id.height) == id.hash)
    handler_.OnComplete(weak);

  MaybeFlush();
}

inline void BlockSync::HandleError(const net::WeakPeer& peer, consensus::Error error) {
  const auto msg = std::format("Validation error code {}.", static_cast<int>(error));

  // Drops peer immediately, and potentially applies misbehavior penalties.
  handler_.OnError(peer, msg);

  // Removes any queued blocks, and deletes any in-flight block download requests.
  queue_.Clear();
  queue_bytes_ = 0;
  {
    std::lock_guard lock(request_mutex_);
    pending_.clear();
    last_request_ = {};
  }

  // TODO: In a multi-peer design, we would need to track which blocks came from which peer,
  // and delete only the downstream blocks and requests from a misbehaving peer.
}

inline void BlockSync::MaybeFlush() const {
  const int64_t dt_ns = perf_.TickNs();
  if (dt_ns <= 0) return;
  
  const auto per_s = [dt_ns](const auto& counter) -> int64_t {
    return (counter.Delta() * 1'000'000'000ll) / dt_ns;
  };
  util::NotifyMetric("SyncManager/BlockSync", util::NotificationMap{
    {"blocks_per_s", per_s(perf_.validated)},
    {"blocks_requested", (int64_t)perf_.pending},
    {"validation_queue", (int64_t)queue_bytes_}
  });
  perf_.Snapshot();
}

}  // namespace hornet::node::sync

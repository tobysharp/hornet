// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <optional>
#include <thread>
#include <variant>

#include "consensus/validator.h"
#include "data/header_context.h"
#include "data/header_timechain.h"
#include "message/getheaders.h"
#include "message/headers.h"
#include "net/peer.h"
#include "protocol/block_header.h"
#include "protocol/hash.h"
#include "util/thread_safe_queue.h"

namespace hornet::data {

// HeaderSync performs header synchronization. It receives headers messages from peers, validates
// them against consensus rules in a background thread, and adds them to the header timechain.
class HeaderSync {
 public:
  using OnError =
      std::function<void(net::PeerId, const protocol::BlockHeader&, consensus::HeaderError)>;
  using OnNeedHeaders = std::function<void(const std::shared_ptr<net::Peer>&, message::GetHeaders&&)>;

  HeaderSync(HeaderTimechain& timechain);
  ~HeaderSync();

  // Sets the maximum number of items allowed in the queue.
  void SetMaxQueueSize(int max_queue_size) {
    max_queue_items_ = max_queue_size;
  }

  // Registers a peer to use for headers sync, and provides its getheaders callback.
  void RegisterPeer(net::PeerId id, OnNeedHeaders callback);

  // Queues a headers message received from a peer for validation.
  void OnHeaders(net::PeerId peer, const message::Headers& message, OnError on_error);

  // Returns true if there is validation work to be done.
  bool HasPendingWork() const {
    return !queue_.Empty();
  }

 private:
  using Batch = std::vector<protocol::BlockHeader>;

  struct Item {
    net::PeerId peer;
    Batch batch;
    OnError on_error;
  };

  // Validates queued headers, and adds them to the headers timechain.
  void Process();

  // Requests more headers via the callback supplied in RegisterPeer.
  void RequestHeadersFrom(net::PeerId id, const protocol::Hash& previous);

  // Calls error handler and deletes peer's other queued items.
  void HandleError(const Item& item, const protocol::BlockHeader& header,
                   consensus::HeaderError error);

  // Returns true if a batch contains the full 2,000 headers.
  static bool IsFullBatch(std::span<const protocol::BlockHeader> batch) {
    return std::ssize(batch) == protocol::kMaxBlockHeaders;
  }

  util::ThreadSafeQueue<Item> queue_;  // Queue of unverified headers to process.
  HeaderTimechain& timechain_;         // Timechain to receive validated headers.
  consensus::Validator validator_;     // Performs consensus rule checks.
  std::thread worker_thread_;          // Background worker thread for processing.
  int max_queue_items_ = 16;           // Default queue capacity to hide download latency.
  OnNeedHeaders on_getheaders_;        // Callback for sending a getheaders message.
  std::atomic_flag send_blocked_;      // Whether getheaders messages are currently blocked.
};

inline HeaderSync::HeaderSync(HeaderTimechain& timechain)
    : timechain_(timechain), worker_thread_([this] { this->Process(); }) {}

inline HeaderSync::~HeaderSync() {
  queue_.Stop();
  worker_thread_.join();
}

// Requests more headers via the callback supplied in RegisterPeer.
inline void HeaderSync::RequestHeadersFrom(net::PeerId id, const protocol::Hash& previous) {
  const auto peer = net::Peer::FromId(id);
  if (peer && on_getheaders_ && queue_.Size() < max_queue_items_) {
    if (!send_blocked_.test_and_set(std::memory_order_acquire)) {
      message::GetHeaders getheaders{peer->GetCapabilities().GetVersion()};
      getheaders.AddLocatorHash(previous);
      on_getheaders_(peer, std::move(getheaders));
    }
  }
}

// Registers a peer to use for headers sync, and provides its getheaders callback.
inline void HeaderSync::RegisterPeer(net::PeerId id, OnNeedHeaders callback) {
  on_getheaders_ = callback;
  send_blocked_.clear(std::memory_order::release);
  RequestHeadersFrom(id, timechain_.HeaviestTip().second->hash);
}

// Queues a headers message received from a peer for validation.
inline void HeaderSync::OnHeaders(net::PeerId peer, const message::Headers& message,
                                  OnError on_error) {
  Assert(send_blocked_.test());
  const auto& headers = message.GetBlockHeaders();

  // Pushes work onto the thread-safe async work queue.
  queue_.Push({peer, Batch{headers.begin(), headers.end()}, on_error});

  if (IsFullBatch(headers)) {
    send_blocked_.clear(std::memory_order::release);
    RequestHeadersFrom(peer, headers.back().ComputeHash());
  }
}

// Validates queued headers, and adds them to the headers timechain.
inline void HeaderSync::Process() {
  for (std::optional<Item> item; (item = queue_.WaitPop());) {
    if (item->batch.empty()) continue;

    // As soon as we pop from the queue, request new headers if appropriate.
    const auto& last_item = queue_.Empty() ? *item : queue_.Back();
    RequestHeadersFrom(item->peer, last_item.batch.back().ComputeHash());

    // Locates the parent of this header in the timechain.
    auto [parent_iterator, parent_context] = timechain_.Find(item->batch[0].GetPreviousBlockHash());
    if (!parent_iterator || !parent_context) {
      HandleError(*item, item->batch[0], consensus::HeaderError::ParentNotFound);
      continue;
    }

    // Creates an implementation-independent view onto the timechain history for the validator.
    const std::unique_ptr<const HeaderTimechain::ValidationView> view =
        timechain_.GetValidationView(parent_iterator);

    for (const auto& header : item->batch) {
      // Validates the header against consensus rules.
      const auto validated = validator_.ValidateDownloadedHeader(*parent_context, header, *view);

      // Handles consensus failures, breaking out of this batch.
      if (const auto* error = std::get_if<consensus::HeaderError>(&validated)) {
        // Notifies caller of consensus failure and discards future batches from the same peer.
        HandleError(*item, header, *error);
        break;
      }

      // Adds the validated header to the headers timechain.
      const auto& context = std::get<data::HeaderContext>(validated);
      parent_iterator = timechain_.Add(context, parent_iterator);
      parent_context = context;
    }
  }
}

// Calls error handler and deletes peer's other queued items.
inline void HeaderSync::HandleError(const Item& item, const protocol::BlockHeader& header,
                                    consensus::HeaderError error) {
  item.on_error(item.peer, header, error);
  queue_.EraseIf([&](const Item& queued) { return net::Peer::IsSame(item.peer, queued.peer); });
}

}  // namespace hornet::data
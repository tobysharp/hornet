// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <optional>
#include <thread>
#include <variant>

#include "hornetlib/consensus/validator.h"
#include "hornetlib/data/header_context.h"
#include "hornetlib/data/header_timechain.h"
#include "hornetlib/message/getheaders.h"
#include "hornetlib/message/headers.h"
#include "hornetlib/net/peer.h"
#include "hornetlib/node/sync_handler.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/util/thread_safe_queue.h"

namespace hornet::node {

// HeaderSync performs header synchronization. It receives headers messages from peers, validates
// them against consensus rules in a background thread, and adds them to the header timechain.
class HeaderSync {
 public:
  HeaderSync(data::HeaderTimechain& timechain, SyncHandler& handler);
  ~HeaderSync();

  // Sets the maximum number of items allowed in the queue.
  void SetMaxQueueSize(int max_queue_size) {
    max_queue_items_ = max_queue_size;
  }

  // Begins downloading and validating headers from a given peer.
  void StartSync(net::PeerId id);

  // Queues a headers message received from a peer for validation.
  void OnHeaders(net::PeerId peer, const message::Headers& message);

  // Returns true if there is validation work to be done.
  bool HasPendingWork() const {
    return !queue_.Empty();
  }

 private:
  using Batch = std::vector<protocol::BlockHeader>;

  struct Item {
    net::PeerId peer;
    Batch batch;
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

  data::HeaderTimechain& timechain_;   // Timechain to receive validated headers.
  SyncHandler& handler_;               // Callbacks for peer-related behavior.
  util::ThreadSafeQueue<Item> queue_;  // Queue of unverified headers to process.
  consensus::Validator validator_;     // Performs consensus rule checks.
  std::thread worker_thread_;          // Background worker thread for processing.
  int max_queue_items_ = 16;           // Default queue capacity to hide download latency.
  std::atomic_flag send_blocked_;      // Whether getheaders messages are currently blocked.
};

inline HeaderSync::HeaderSync(data::HeaderTimechain& timechain, SyncHandler& handler)
    : timechain_(timechain), handler_(handler), worker_thread_([this] { this->Process(); }) {}

inline HeaderSync::~HeaderSync() {
  queue_.Stop();
  worker_thread_.join();
}

// Requests more headers via the callback supplied in RegisterPeer.
inline void HeaderSync::RequestHeadersFrom(net::PeerId id, const protocol::Hash& previous) {
  if (queue_.Size() < max_queue_items_) {
    if (!send_blocked_.test_and_set(std::memory_order_acquire)) {
      const auto peer = net::Peer::FromId(id);
      const int version = peer ? peer->GetCapabilities().GetVersion() : protocol::kCurrentVersion;
      message::GetHeaders getheaders{version};
      getheaders.AddLocatorHash(previous);
      handler_.OnRequest(peer, std::make_unique<message::GetHeaders>(std::move(getheaders)));
    }
  }
}

  // Begins downloading and validating headers from a given peer.
inline void HeaderSync::StartSync(net::PeerId id) {
  send_blocked_.clear(std::memory_order::release);
  RequestHeadersFrom(id, timechain_.HeaviestTip().second->hash);
}

// Queues a headers message received from a peer for validation.
inline void HeaderSync::OnHeaders(net::PeerId peer, const message::Headers& message) {
  Assert(send_blocked_.test());
  const auto& headers = message.GetBlockHeaders();

  // Pushes work onto the thread-safe async work queue.
  queue_.Push({peer, Batch{headers.begin(), headers.end()}});

  if (IsFullBatch(headers)) {
    send_blocked_.clear(std::memory_order::release);
    RequestHeadersFrom(peer, headers.back().ComputeHash());
  }
}

// Validates queued headers, and adds them to the headers timechain.
inline void HeaderSync::Process() {
  for (std::optional<Item> item; (item = queue_.WaitPop());) {

    if (!item->batch.empty()) {
      // As soon as we pop from the queue, request new headers if appropriate.
      const auto& last_item = queue_.Empty() ? *item : queue_.Back();
      if (IsFullBatch(last_item.batch))
        RequestHeadersFrom(item->peer, last_item.batch.back().ComputeHash());

      // Locates the parent of this header in the timechain.
      auto [parent_iterator, parent_context] = timechain_.Find(item->batch[0].GetPreviousBlockHash());
      if (!parent_iterator || !parent_context) {
        HandleError(*item, item->batch[0], consensus::HeaderError::ParentNotFound);
        continue;
      }

      // Creates an implementation-independent view onto the timechain history for the validator.
      const std::unique_ptr<data::HeaderTimechain::ValidationView> view =
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
        view->SetTip(parent_iterator = timechain_.Add(context, parent_iterator));
        parent_context = context;
      }
    }

    // Notify if the sync is complete.
    if (!IsFullBatch(item->batch)) {
      handler_.OnComplete(item->peer);
    }
  }
}

// Calls error handler and deletes peer's other queued items.
inline void HeaderSync::HandleError(const Item& item, const protocol::BlockHeader&,
                                    consensus::HeaderError error) {
  std::ostringstream oss;
  oss << "Header validation error code " << static_cast<int>(error) << ".";
  handler_.OnError(item.peer, oss.str());
  queue_.EraseIf([&](const Item& queued) { return net::Peer::IsSame(item.peer, queued.peer); });
}

}  // namespace hornet::node
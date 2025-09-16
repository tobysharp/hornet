// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <optional>
#include <thread>
#include <variant>

#include "hornetlib/consensus/rules/validate.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/data/timechain.h"
#include "hornetlib/model/header_context.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/message/getheaders.h"
#include "hornetlib/protocol/message/headers.h"
#include "hornetlib/util/notify.h"
#include "hornetlib/util/thread_safe_queue.h"
#include "hornetnodelib/net/peer.h"
#include "hornetnodelib/sync/sync_handler.h"

namespace hornet::node::sync {

// HeaderSync performs header synchronization. It receives headers messages from peers, validates
// them against consensus rules in a background thread, and adds them to the header timechain.
class HeaderSync {
 public:
  HeaderSync(data::Timechain& timechain, SyncHandler& handler);
  ~HeaderSync();

  // Sets the maximum number of items allowed in the queue.
  void SetMaxQueueSize(int max_queue_size) {
    max_queue_items_ = max_queue_size;
  }

  // Begins downloading and validating headers from a given peer.
  void StartSync(net::WeakPeer id);

  // Queues a headers message received from a peer for validation.
  void OnHeaders(net::WeakPeer peer, const protocol::message::Headers& message);

  // Returns true if there is validation work to be done.
  bool HasPendingWork() const {
    return !queue_.Empty();
  }

 private:
  using Batch = std::vector<protocol::BlockHeader>;

  struct Item {
    net::WeakPeer weak_peer;
    Batch batch;
  };

  // Validates queued headers, and adds them to the headers timechain.
  void Process();

  // Requests more headers via the callback supplied in RegisterPeer.
  bool RequestHeadersFrom(net::WeakPeer id);

  // Calls error handler and deletes peer's other queued items.
  void HandleError(const Item& item, const protocol::BlockHeader& header,
                   consensus::HeaderError error);

  // Returns true if a batch contains the full 2,000 headers.
  static bool IsFullBatch(std::span<const protocol::BlockHeader> batch) {
    return std::ssize(batch) == protocol::kMaxBlockHeaders;
  }

  data::Timechain& timechain_;         // Timechain to receive validated headers.
  SyncHandler& handler_;               // Callbacks for peer-related behavior.
  util::ThreadSafeQueue<Item> queue_;  // Queue of unverified headers to process.
  std::thread worker_thread_;          // Background worker thread for processing.
  int max_queue_items_ = 16;           // Default queue capacity to hide download latency.
  std::atomic_flag send_blocked_;      // Whether getheaders messages are currently blocked.
  protocol::Hash next_request_ = {};   // Hash of last header to arrive for next request.
};

inline HeaderSync::HeaderSync(data::Timechain& timechain, SyncHandler& handler)
    : timechain_(timechain), handler_(handler), worker_thread_([this] { this->Process(); }) {}

inline HeaderSync::~HeaderSync() {
  queue_.Stop();
  worker_thread_.join();
}

// Requests more headers via the callback supplied in RegisterPeer.
inline bool HeaderSync::RequestHeadersFrom(net::WeakPeer weak_peer) {
  if (queue_.Size() < max_queue_items_) {
    if (!send_blocked_.test_and_set(std::memory_order_acquire)) {
      bool ok = false;
      if (next_request_) {
        const auto peer = weak_peer.lock();
        const int version = peer ? peer->GetCapabilities().GetVersion() : protocol::kCurrentVersion;
        protocol::message::GetHeaders getheaders{version};
        getheaders.AddLocatorHash(next_request_);
        ok = handler_.OnRequest(peer, std::make_unique<protocol::message::GetHeaders>(std::move(getheaders)));
      }
      if (!ok)
        send_blocked_.clear();
      return ok;
    }
  }
  return false;
}

  // Begins downloading and validating headers from a given peer.
inline void HeaderSync::StartSync(net::WeakPeer peer) {
  next_request_ = timechain_.ReadHeaders()->ChainTip()->hash;
  send_blocked_.clear(std::memory_order::release);
  if (!RequestHeadersFrom(peer))
    handler_.OnComplete(peer);  // No headers will ever reach the queue.
}

// Queues a headers message received from a peer for validation.
inline void HeaderSync::OnHeaders(net::WeakPeer peer, const protocol::message::Headers& message) {
  Assert(send_blocked_.test());
  const auto& headers = message.GetBlockHeaders();

  // Pushes work onto the thread-safe async work queue.
  queue_.Push({peer, Batch{headers.begin(), headers.end()}});

  if (IsFullBatch(headers)) {
    next_request_ = headers.back().ComputeHash();
    send_blocked_.clear(std::memory_order::release);
    RequestHeadersFrom(peer);
  }
  else
    next_request_ = {};  // No more headers to be requested.
}

// Validates queued headers, and adds them to the headers timechain.
inline void HeaderSync::Process() {
  for (std::optional<Item> item; (item = queue_.WaitPop());) {

    if (!item->batch.empty()) {
      // As soon as we pop from the queue, request new headers if appropriate.
      RequestHeadersFrom(item->weak_peer);

      // Locates the parent of this header in the timechain.
      auto headers = timechain_.ReadHeaders();
      auto parent = headers->Search(item->batch[0].GetPreviousBlockHash());
      if (!parent) {
        HandleError(*item, item->batch[0], consensus::HeaderError::ParentNotFound);
        continue;
      }

      // Creates an implementation-independent view onto the timechain history for the validator.
      const std::unique_ptr<data::HeaderTimechain::ValidationView> view =
          headers->GetValidationView(parent);

      for (const auto& header : item->batch) {
        // Validates the header against consensus rules.
        const auto validated = consensus::ValidateHeader(*parent, header, *view);

        // Handles consensus failures, breaking out of this batch.
        if (!validated) {
          // Notifies caller of consensus failure and discards future batches from the same peer.
          HandleError(*item, header, validated.Error());
          break;
        }

        // Adds the validated header to the headers timechain.
        view->SetTip(parent = timechain_.AddHeader(parent, parent->Extend(header)));
      }
    }

    util::NotifyMetric("sync/headers", {{"headers_validated",timechain_.ReadHeaders()->ChainLength()}});

    // Notify if the sync is complete.
    if (!IsFullBatch(item->batch)) {
      handler_.OnComplete(item->weak_peer);
    }
  }
}

// Calls error handler and deletes peer's other queued items.
inline void HeaderSync::HandleError(const Item& item, const protocol::BlockHeader&,
                                    consensus::HeaderError error) {
  std::ostringstream oss;
  oss << "Header validation error code " << static_cast<int>(error) << ".";
  handler_.OnError(item.weak_peer, oss.str());
  queue_.EraseIf([&](const Item& queued) { return item.weak_peer == queued.weak_peer; });
}

}  // namespace hornet::node::sync
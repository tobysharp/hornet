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
#include "util/thread_safe_queue.h"

namespace hornet::data {

// HeaderSync performs header synchronization. It receives headers messages from peers, validates
// them against consensus rules in a background thread, and adds them to the header timechain.
class HeaderSync {
 public:
  using OnError =
      std::function<void(net::PeerId, const protocol::BlockHeader&, consensus::HeaderError)>;

  HeaderSync(HeaderTimechain& timechain);
  ~HeaderSync();

  // Returns a getheaders message that may be sent to the peer to begin header sync.
  std::optional<message::GetHeaders> Initiate(net::PeerId id);

  // Queues a headers message received from a peer for validation.
  // Returns a getheaders message if more headers are needed from the same peer.
  std::optional<message::GetHeaders> OnHeaders(net::PeerId peer, const message::Headers& message,
                                               OnError on_error);

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

  // Calls error handler and deletes peer's other queued items.
  void HandleError(const Item& item, const protocol::BlockHeader& header, consensus::HeaderError error);

  util::ThreadSafeQueue<Item> queue_;  // Queue of unverified headers to process.
  HeaderTimechain& timechain_;         // Timechain to receive validated headers.
  consensus::Validator validator_;     // Performs consensus rule checks.
  std::thread worker_thread_;          // Background worker thread for processing.
};

inline HeaderSync::HeaderSync(HeaderTimechain& timechain)
    : timechain_(timechain), worker_thread_([this] { this->Process(); }) {}

inline HeaderSync::~HeaderSync() {
  queue_.Stop();
  worker_thread_.join();
}

// Returns a getheaders message that may be sent to the peer to begin header sync.
inline std::optional<message::GetHeaders> HeaderSync::Initiate(net::PeerId id) {
  const auto tip = timechain_.HeaviestTip();
  if (!tip.second) return {};
  const auto peer = net::Peer::FromId(id);
  message::GetHeaders getheaders(peer->GetCapabilities().GetVersion());
  getheaders.AddLocatorHash(tip.second->hash);
  return getheaders;
}

// Queues a headers message received from a peer for validation.
// Returns a getheaders message if more headers are needed from the same peer.
inline std::optional<message::GetHeaders> HeaderSync::OnHeaders(net::PeerId peer,
                                                                const message::Headers& message,
                                                                OnError on_error) {
  const auto& headers = message.GetBlockHeaders();

  // Pushes work onto the thread-safe async work queue.
  queue_.Push({peer, Batch{headers.begin(), headers.end()}, on_error});

  if (headers.size() == protocol::kMaxBlockHeaders) {
    // Requests more headers from the same peer.
    message::GetHeaders getheaders(net::Peer::FromId(peer)->GetCapabilities().GetVersion());
    getheaders.AddLocatorHash(headers.back().ComputeHash());
    return getheaders;
  }
  return {};
}

// Validates queued headers, and adds them to the headers timechain.
inline void HeaderSync::Process() {
  for (std::optional<Item> item; (item = queue_.WaitPop());) {
    if (item->batch.empty()) continue;

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
inline void HeaderSync::HandleError(const Item& item, const protocol::BlockHeader& header, consensus::HeaderError error) {
  item.on_error(item.peer, header, error);
  queue_.EraseIf([&](const Item& queued) { return net::Peer::IsSame(item.peer, queued.peer); });
}

}  // namespace hornet::data
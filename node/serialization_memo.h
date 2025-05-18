#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "node/outbound_message.h"
#include "protocol/constants.h"
#include "protocol/framer.h"

namespace hornet::node {

class SerializationMemo {
 public:
  SerializationMemo(OutboundMessage&& message) : message_(std::move(message)) {}

  std::shared_ptr<const std::vector<uint8_t>> GetSerializedBuffer(protocol::Magic magic) {
    if (serialized_) return serialized_;
    std::lock_guard lock(serialize_mutex_);
    if (!serialized_) {
      auto shared = protocol::Framer::FrameToBuffer(magic, message_->GetMessage());
      serialized_ = std::static_pointer_cast<const std::vector<uint8_t>>(shared);
      message_.reset();
    }
    return serialized_;
  }

 private:
  std::optional<OutboundMessage> message_;
  std::shared_ptr<const std::vector<uint8_t>> serialized_;
  std::mutex serialize_mutex_;
};

using SerializationMemoPtr = std::shared_ptr<SerializationMemo>;

}  // namespace hornet::node

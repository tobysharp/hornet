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

  std::shared_ptr<const std::vector<uint8_t>> GetSerializedBuffer(protocol::Magic magic) const {
    std::lock_guard lock(serialize_mutex_);
    if (!serialized_ && message_) {
      const auto buffer = protocol::Framer::FrameToBuffer(magic, message_->GetMessage());
      serialized_ = std::make_shared<const std::vector<uint8_t>>(std::move(buffer));
      message_.reset();
    }
    return serialized_;
  }

  friend std::ostream& operator <<(std::ostream& os, const SerializationMemo& memo) {
    std::lock_guard lock(memo.serialize_mutex_);
    if (memo.message_)
      return os << *memo.message_; 
    else
      return os << "<empty>";
  }
  
 private:
  mutable std::optional<OutboundMessage> message_;
  mutable std::shared_ptr<const std::vector<uint8_t>> serialized_;
  mutable std::mutex serialize_mutex_;
};

using SerializationMemoPtr = std::shared_ptr<SerializationMemo>;

}  // namespace hornet::node

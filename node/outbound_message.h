#pragma once

#include <chrono>
#include <memory>

#include "protocol/message.h"

namespace hornet::node {

class OutboundMessage {
 public:
  OutboundMessage(std::unique_ptr<const protocol::Message>&& msg)
      : message_(std::move(msg)), construct_time_(std::chrono::steady_clock::now()) {}

 private:
  std::unique_ptr<const protocol::Message> message_;
  std::chrono::steady_clock::time_point construct_time_;
};

using OutboundMessagePtr = std::shared_ptr<OutboundMessage>;

}  // namespace hornet::node

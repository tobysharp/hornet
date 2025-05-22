#pragma once

#include <chrono>
#include <memory>

#include "protocol/message.h"

namespace hornet::node {

class OutboundMessage {
 public:
  OutboundMessage(std::unique_ptr<const protocol::Message>&& msg)
      : message_(std::move(msg)), construct_time_(std::chrono::steady_clock::now()) {}

  const protocol::Message& GetMessage() const {
    return *message_;
  }

  friend std::ostream& operator <<(std::ostream& os, const OutboundMessage& m) {
    return os << "{ message = " << *m.message_ << " }";
  }
 private:
  std::unique_ptr<const protocol::Message> message_;
  std::chrono::steady_clock::time_point construct_time_;
  // TODO: Priority, origination, etc.
};

using OutboundMessagePtr = std::shared_ptr<OutboundMessage>;

}  // namespace hornet::node

// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <chrono>
#include <format>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "hornetlib/protocol/message_handler.h"
#include "hornetlib/util/throw.h"

// Forward declarations;
namespace hornet::encoding {
class Writer;
class Reader;
}  // namespace hornet::encoding

namespace hornet::protocol {

class Message {
 public:
  enum class Direction { Inbound, Outbound };

  struct Envelope {
    Direction direction;
    uint64_t peer_id;
    std::chrono::system_clock::time_point timestamp;
    // Optional: priority
  };

  // Virtual methods
  virtual ~Message() = default;
  virtual void Serialize(encoding::Writer&) const {}
  virtual void Deserialize(encoding::Reader&) {}
  virtual std::string GetName() const = 0;
  virtual void Notify(MessageHandler& handler) const {
    handler.OnMessage(*this);
  }
  virtual void PrintTo(std::ostream& os) const {
    os << GetName();  // Fallback if not overridden.
  }

  // Properties
  const std::optional<Envelope>& GetEnvelope() const {
    return envelope_;
  }
  bool HasEnvelope() const {
    return envelope_.has_value();
  }
  void SetEnvelope(const Envelope& envelope) {
    envelope_ = envelope;
  }
  void ClearEnvelope() {
    envelope_.reset();
  }
  std::string ToString() const {
    std::ostringstream os;
    os << "[";
    if (envelope_) {
      const std::string time = std::format("{:%Y-%m-%d %H:%M:%S}", envelope_->timestamp);
      os << (envelope_->direction == Direction::Inbound ? "from " : "to ")
         << envelope_->peer_id << " at " << time;
    }
    os << "] ";
    PrintTo(os);
    return os.str();
  }

  // Operators
  friend std::ostream& operator <<(std::ostream& os, const Message& msg) {
    return os << msg.ToString();
  }
 protected:
  std::optional<Envelope> envelope_;
};

template <typename T>
std::unique_ptr<T> Downcast(std::unique_ptr<Message>&& msg) {
  if (auto* ptr = dynamic_cast<T*>(msg.get())) {
    msg.release();
    return std::unique_ptr<T>{ptr};
  }
  util::ThrowRuntimeError("Message dynamic downcast to ", typeid(T).name(), " failed.");
}

}  // namespace hornet::protocol

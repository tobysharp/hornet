#pragma once

#include <string>

#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/protocol/message.h"
#include "hornetlib/protocol/message_handler.h"

namespace hornet::protocol::message {

class Block : public Message {
 public:
  virtual std::string GetName() const override {
    return "block";
  }
  virtual void Notify(MessageHandler& handler) const override {
    handler.OnMessage(*this);
  }
  virtual void Serialize(encoding::Writer& writer) const override {
    block_.Serialize(writer);
  }
  virtual void Deserialize(encoding::Reader& reader) override {
    block_.Deserialize(reader);
  };

 protected:
  protocol::Block block_;
};

}  // namespace hornet::protocol::message

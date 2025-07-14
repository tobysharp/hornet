#pragma once

#include <string>

#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/protocol/message.h"
#include "hornetlib/protocol/message_handler.h"

namespace hornet::protocol::message {

class Block : public Message {
 public:
  std::shared_ptr<const protocol::Block> GetBlock() const {
    return block_;
  }
  virtual std::string GetName() const override {
    return "block";
  }
  virtual void Notify(MessageHandler& handler) const override {
    handler.OnMessage(*this);
  }
  virtual void Serialize(encoding::Writer& writer) const override {
    if (block_)
      block_->Serialize(writer);
  }
  virtual void Deserialize(encoding::Reader& reader) override {
    protocol::Block block;
    block.Deserialize(reader);
    block_ = std::make_shared<const protocol::Block>(std::move(block));
  };

 protected:
  std::shared_ptr<const protocol::Block> block_;
};

}  // namespace hornet::protocol::message

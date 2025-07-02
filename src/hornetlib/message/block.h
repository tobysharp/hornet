#pragma once

#include <string>

#include "hornetlib/message/visitor.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/message.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::message {

class Block : public protocol::Message {
 public:
  virtual std::string GetName() const override {
    return "block";
  }
  virtual void Accept(Visitor& visitor) const override {
    visitor.Visit(*this);
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

}  // namespace hornet::message

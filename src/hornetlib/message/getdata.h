#pragma once

#include <vector>

#include "hornetlib/encoding/reader.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/message/visitor.h"
#include "hornetlib/protocol/inventory.h"
#include "hornetlib/protocol/message.h"

namespace hornet::message {

class GetData : public protocol::Message {
 public:
  void AddInventory(const protocol::Inventory& inv) {
    inventory_.push_back(inv);
  }

  virtual std::string GetName() const override {
    return "getdata";
  }

  virtual void Accept(message::Visitor& v) const override {
    v.Visit(*this);
  }

  virtual void Serialize(encoding::Writer& writer) const override {
    writer.WriteVarInt(inventory_.size());
    for (const auto& inv : inventory_)
      protocol::Inventory::Transfer(writer, inv);
  }

  virtual void Deserialize(encoding::Reader& reader) override {
    inventory_.resize(reader.ReadVarInt<size_t>());
    for (auto& inv : inventory_)
      protocol::Inventory::Transfer(reader, inv);
  }

 private:
  std::vector<protocol::Inventory> inventory_;
};

}  // namespace hornet::message

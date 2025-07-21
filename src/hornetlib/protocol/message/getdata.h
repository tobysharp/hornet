// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <vector>

#include "hornetlib/encoding/reader.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/protocol/inventory.h"
#include "hornetlib/protocol/message.h"
#include "hornetlib/protocol/message_handler.h"

namespace hornet::protocol::message {

class GetData : public Message {
 public:
  void AddInventory(const Inventory& inv) {
    inventory_.push_back(inv);
  }

  virtual std::string GetName() const override {
    return "getdata";
  }

  virtual void Notify(MessageHandler& handler) const override {
    handler.OnMessage(*this);
  }

  virtual void Serialize(encoding::Writer& writer) const override {
    writer.WriteVarInt(inventory_.size());
    for (const auto& inv : inventory_)
      Inventory::Transfer(writer, inv);
  }

  virtual void Deserialize(encoding::Reader& reader) override {
    inventory_.resize(reader.ReadVarInt<size_t>());
    for (auto& inv : inventory_)
      Inventory::Transfer(reader, inv);
  }

 private:
  std::vector<Inventory> inventory_;
};

}  // namespace hornet::protocol::message

// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <string>

#include "hornetlib/protocol/message.h"
#include "hornetlib/protocol/message_handler.h"

namespace hornet::protocol::message {

class Verack : public Message {
  public:
    virtual std::string GetName() const override {
        return "verack";
    }
    virtual void Notify(MessageHandler& handler) const override {
      handler.OnMessage(*this);
    }
};

}  // namespace hornet::protocol::message

// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <string>

#include "message/visitor.h"
#include "protocol/message.h"

namespace hornet::message {

class Verack : public protocol::Message {
  public:
    virtual std::string GetName() const override {
        return "verack";
    }
    virtual void Accept(Visitor& v) const override {
      return v.Visit(*this);
    }
};

}  // namespace hornet::message

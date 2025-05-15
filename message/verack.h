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

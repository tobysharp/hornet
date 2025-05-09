#pragma once

#include <string>

#include "protocol/message.h"

namespace hornet::message {

class Verack : public protocol::Message {
  public:
    virtual std::string GetName() const override {
        return "verack";
    }
};

}  // namespace hornet::message

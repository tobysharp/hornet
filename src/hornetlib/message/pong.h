// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "hornetlib/encoding/reader.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/message/visitor.h"
#include "hornetlib/protocol/message.h"

namespace hornet::message {

class Pong : public protocol::Message {
 public:
  Pong(uint64_t nonce = 0) : nonce_(nonce) {}

  uint64_t GetNonce() const {
    return nonce_;
  }
  virtual std::string GetName() const override {
    return "pong";
  }
  virtual void Serialize(encoding::Writer& w) const override {
    w.WriteLE8(nonce_);
  }
  virtual void Deserialize(encoding::Reader& r) override {
    r.ReadLE8(nonce_);
  }
  virtual void Accept(message::Visitor& v) const override {
    return v.Visit(*this);
  }

 private:
  uint64_t nonce_;
};

}  // namespace hornet::message
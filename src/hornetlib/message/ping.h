// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "hornetlib/encoding/reader.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/message/visitor.h"
#include "hornetlib/protocol/message.h"
#include "hornetlib/util/rand.h"

namespace hornet::message {

class Ping : public protocol::Message {
 public:
  Ping() : nonce_(util::Rand64()) {}

  uint64_t GetNonce() const {
    return nonce_;
  }

  virtual std::string GetName() const override {
    return "ping";
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

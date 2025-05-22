#pragma once

#include "encoding/reader.h"
#include "encoding/writer.h"
#include "message/visitor.h"
#include "protocol/message.h"

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
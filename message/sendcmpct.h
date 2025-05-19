#pragma once

#include "encoding/reader.h"
#include "encoding/writer.h"
#include "protocol/message.h"
#include "message/visitor.h"

namespace hornet::message {

class SendCompact : public protocol::Message {
 public:
  bool IsCompact() const { return flag_; }
  int GetVersion() const { return version_; }
  virtual std::string GetName() const override {
    return "sendcmpct";
  }
  virtual void Accept(Visitor& v) const override {
    v.Visit(*this);
  }
  virtual void Serialize(encoding::Writer& w) const override {
    w.WriteBool(flag_);
    w.WriteLE8(version_);
  }
  virtual void Deserialize(encoding::Reader& r) override {
    r.ReadBool(flag_);
    r.ReadLE8(version_);
  }
 private:
  bool flag_ = true;
  int version_ = 1;
};

}  // namespace hornet::message
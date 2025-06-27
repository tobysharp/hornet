// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <vector>

#include "hornetlib/message/visitor.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/message.h"
#include "hornetlib/util/throw.h"

namespace hornet::message {

class Headers : public protocol::Message {
 public:
  Headers() = default;
  Headers(const Headers&) = default;
  Headers(Headers&&) = default;
  Headers(const Headers&&) = delete;

  std::span<const protocol::BlockHeader> GetBlockHeaders() const {
    return block_headers_;
  }
  void AddBlockHeader(const protocol::BlockHeader& header) {
    block_headers_.emplace_back(header);
  }
  virtual std::string GetName() const override { return "headers"; }
  virtual void Accept(Visitor& v) const override {
    v.Visit(*this);
  }
  virtual void Serialize(encoding::Writer& w) const override {
    const auto size = block_headers_.size();
    if (size > protocol::kMaxBlockHeaders)
        util::ThrowOutOfRange("Too many block headers to serialize message: ", size, ".");
    w.WriteVarInt(size);
    for (const auto& header : block_headers_)
        header.Serialize(w);
  }
  virtual void Deserialize(encoding::Reader& r) override {
    const auto size = r.ReadVarInt<size_t>();
    if (size > protocol::kMaxBlockHeaders)
        util::ThrowOutOfRange("Too many block headers to deserialize message: ", size, ".");
    block_headers_.resize(size);
    for (auto& header : block_headers_)
        header.Deserialize(r);
  }
  virtual void PrintTo(std::ostream& os) const override {
    os << "Headers{ block_headers.size = " << block_headers_.size() << " }";
  }
 private:
  std::vector<protocol::BlockHeader> block_headers_;
};

}  // namespace hornet::message

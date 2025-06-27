// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "hornetlib/crypto/hash.h"
#include "hornetlib/encoding/reader.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/message/visitor.h"
#include "hornetlib/protocol/constants.h"
#include "hornetlib/protocol/message.h"

namespace hornet::message {

class GetHeaders : public protocol::Message {
 public:
  GetHeaders() : version_(protocol::kCurrentVersion) {}
  explicit GetHeaders(int version) : version_(version) {}
  GetHeaders(const GetHeaders&) = default;
  GetHeaders(GetHeaders&&) = default;
  GetHeaders(const GetHeaders&&) = delete;

  void AddLocatorHash(const crypto::bytes32_t& hash) {
    locator_hashes_.emplace_back(hash);
  }

  void SetStopHash(const crypto::bytes32_t& hash) {
    stop_hash_ = hash;
  }

  virtual std::string GetName() const override {
    return "getheaders";
  }

  virtual void Serialize(encoding::Writer& w) const override {
    w.WriteLE4(version_);
    w.WriteVarInt(locator_hashes_.size());
    for (const auto& hash : locator_hashes_) w.WriteBytes(hash);
    w.WriteBytes(stop_hash_);
  }

  virtual void Deserialize(encoding::Reader& r) override {
    r.ReadLE4(version_);
    const auto size = r.ReadVarInt<size_t>();
    if (size > protocol::kMaxBlockLocatorHashes) {
      util::ThrowRuntimeError("getheaders with ", size, " locator hashes exceeds maximum.");
    }
    locator_hashes_.resize(size);
    for (auto& hash : locator_hashes_) r.ReadBytes(hash);
    r.ReadBytes(stop_hash_);
  }

  virtual void Accept(message::Visitor& v) const override {
    v.Visit(*this);
  }

 private:
  int version_;
  std::vector<crypto::bytes32_t> locator_hashes_;
  crypto::bytes32_t stop_hash_ = {};
};

}  // namespace hornet::message

#pragma once

#include "crypto/hash.h"
#include "encoding/reader.h"
#include "encoding/writer.h"
#include "message/visitor.h"
#include "protocol/constants.h"
#include "protocol/message.h"

namespace hornet::message {

class GetHeaders : public protocol::Message {
 public:
  explicit GetHeaders(int version) : version_(version) {}

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
  uint32_t version_;
  std::vector<crypto::bytes32_t> locator_hashes_;
  crypto::bytes32_t stop_hash_ = {};
};

}  // namespace hornet::message

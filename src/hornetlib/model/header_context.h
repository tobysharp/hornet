// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <ostream>

#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/work.h"

namespace hornet::model {

struct HeaderContext {
  static HeaderContext Null() {
    return {{}, {}, {}, {}, -1};
  }

  static HeaderContext Genesis(const protocol::BlockHeader& header) {
    const auto work = header.GetWork();
    return HeaderContext{header, header.ComputeHash(), work, work, 0}; 
  }

  HeaderContext Extend(const protocol::BlockHeader& next, const protocol::Hash& hash) const {
    const auto work = next.GetWork();
    return {next, hash, work, total_work + work, height + 1};
  }

  HeaderContext Extend(const protocol::BlockHeader& next) const {
    const auto work = next.GetWork();
    return {next, next.ComputeHash(), work, total_work + work, height + 1};
  }

  HeaderContext Rewind(const protocol::BlockHeader& prev) const {
    const auto hash = data.GetPreviousBlockHash();
    return {prev, hash, prev.GetWork(), total_work - local_work, height - 1};
  }

  friend std::ostream& operator <<(std::ostream& os, const HeaderContext& obj) {
    os << "{";
    os << "\"height\":" << obj.height << ", ";
    os << "\"hash\":" << obj.hash << ", ";
    os << "\"local_work\": " << obj.local_work << ", ";
    os << "\"total_work\": " << obj.total_work;
    os << "}";
    return os;
  }

  protocol::BlockHeader data = {};
  protocol::Hash hash = {};
  protocol::Work local_work;
  protocol::Work total_work;
  int height = -1;
};

}  // namespace hornet::model

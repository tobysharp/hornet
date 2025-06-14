#pragma once

#include "protocol/block_header.h"
#include "protocol/hash.h"
#include "protocol/work.h"

namespace hornet::data {

struct HeaderContext {
  static HeaderContext Null() {
    return {{}, {}, {}, {}, -1};
  }

  static HeaderContext Genesis(protocol::BlockHeader header) {
    const auto work = header.GetWork();
    return {std::move(header), header.ComputeHash(), work, work, 0}; 
  }

  HeaderContext Extend(protocol::BlockHeader header) const {
    const auto work = header.GetWork();
    return {std::move(header), header.ComputeHash(), work, total_work + work, height + 1};
  }

  HeaderContext Rewind(protocol::BlockHeader header) const {
    return {std::move(header), header.ComputeHash(), header.GetWork(), total_work - local_work, height - 1};
  }

  HeaderContext Rewind(protocol::BlockHeader header, const protocol::Hash& hash) const {
    return {std::move(header), hash, header.GetWork(), total_work - local_work, height - 1};
  }

  protocol::BlockHeader header;
  protocol::Hash hash;
  protocol::Work local_work;
  protocol::Work total_work;
  int height;
};

}  // namespace hornet::data

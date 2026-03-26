#pragma once

#include "hornetlib/consensus/algebra.h"
#include "hornetlib/consensus/bips.h"

namespace hornet::consensus::rules {

template <typename Context>
int GetHeight(const Context& context) {
  return context.height;
}

struct IsBIPActive {
  BIP bip;
  template <typename Context>
  bool operator()(const Context& context) const {
    return IsBIPActiveAtHeight(bip, GetHeight(context));
  }
};

template <typename Node>
inline constexpr decltype(auto) From(BIP bip, Node&& node) {
  return When{IsBIPActive{bip}, std::forward<Node>(node)};
}

}  // namespace hornet::consensus::rules

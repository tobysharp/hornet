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

template <typename Fn>
inline constexpr auto From(Fn&& fn, BIP bip) {
  return When{IsBIPActive{bip}, Rule{std::forward<Fn>(fn)}};
}

}  // namespace hornet::consensus::rules

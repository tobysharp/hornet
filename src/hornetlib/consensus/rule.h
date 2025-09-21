#pragma once

#include <iterator>
#include <optional>

#include "hornetlib/consensus/bips.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/util/assert.h"

namespace hornet::consensus {

template <typename Fn>
struct Rule {
  Fn fn;
  std::optional<BIP> bip = std::nullopt;
};

template <size_t N, typename Fn>
using Ruleset = std::array<Rule<Fn>, N>;

template <typename Error, size_t N, typename Fn, typename... Args>
SuccessOr<Error> ValidateRules(const Ruleset<N, Fn>& ruleset, int height, Args&&... args) {
  for (const auto& rule : ruleset) {
    Assert(!(rule.bip && height <= 0));
    if (rule.bip && !IsBIPEnabledAtHeight(*rule.bip, height)) continue;
    if (const auto result = rule.fn(std::forward<Args>(args)...); !result) return result;
  }
  return {};
}

}  // namespace hornet::consensus

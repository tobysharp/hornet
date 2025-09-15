#pragma once

#include <optional>

#include "hornetlib/consensus/bips.h"

namespace hornet::consensus {

template <typename Fn>
struct Rule {
  Fn fn;
  std::optional<BIP> bip = std::nullopt;
};

template <size_t N, typename Fn>
using Ruleset = std::array<Rule<Fn>, N>;

template <size_t N, typename Fn, typename... Args>
auto ValidateRules(const Ruleset<N, Fn>& ruleset, int height, Args&&... args) 
  -> decltype(ruleset[0].fn(std::forward<Args>(args)...))
{
  for (const auto& rule : ruleset) {
    if (rule.bip && !IsBIPEnabledAtHeight(*rule.bip, height)) continue;

    const auto result = rule.fn(std::forward<Args>(args)...);
    if (!result) return result;
  }
  return {};
}

}  // namespace hornet::consensus

#pragma once

#include <iterator>
#include <optional>

#include "hornetlib/consensus/bips.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/util/assert.h"

namespace hornet::consensus {

template <typename Fn, typename Proj = std::identity>
struct Rule {
  Fn fn;
  Proj proj{};
  std::optional<BIP> bip = std::nullopt;

  Rule(Fn f) : fn(std::move(f)) {}
  Rule(Fn f, BIP b) : fn(std::move(f)), bip(b) {}
  Rule(Fn f, Proj p) : fn(std::move(f)), proj(std::move(p)) {}
  Rule(Fn f, Proj p, BIP b) : fn(std::move(f)), proj(std::move(p)), bip(b) {}

  template <typename... Args>
  Result operator()(const int height, Args&&... args) const {
    if (bip && !IsBIPEnabledAtHeight(*bip, height)) return {};
    return fn(proj(std::forward<Args>(args)...));
  }
};

template <typename... Rules, typename... Args>
Result ValidateRules(const std::tuple<Rules...>& ruleset, int height, Args&&... args) {
  return std::apply(
    [&](auto&&... rules) {
      Result rv{};
      ((rv = rv.AndThen([&] { 
        return rules(height, std::forward<Args>(args)...); 
      })), ...);
      return rv;
  }, ruleset);
}

template <typename Rule, size_t N, typename... Args>
Result ValidateRules(const std::array<Rule, N>& ruleset, int height, Args&&... args) {
  Result rv{};
  for (const Rule& rule : ruleset)
    rv = rv.AndThen([&] { return rule(height, std::forward<Args>(args)...); });
  return rv;
}

}  // namespace hornet::consensus

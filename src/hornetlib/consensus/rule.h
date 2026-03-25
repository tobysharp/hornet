#pragma once

#include <iterator>
#include <optional>
#include <type_traits>

#include "hornetlib/consensus/bips.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/util/assert.h"
#include "hornetlib/util/throw.h"

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
    if (bip && height < 0) util::ThrowRuntimeError("Invalid height on height-gated rule.");
    if (bip && !IsBIPActiveAtHeight(*bip, height)) return {};
    return fn(proj(std::forward<Args>(args)...));
  }
};

template <typename Fn, typename Proj = std::identity>
using Group = Rule<Fn, Proj>;

template <typename Fn, typename Enum, typename Proj = std::identity>
struct Each {
  Fn fn;
  Enum iter;
  Proj proj{};

  Each(Fn f, Enum e) : fn(std::move(f)), iter(std::move(e)) {}
  Each(Fn f, Enum e, Proj p) : fn(std::move(f)), iter(std::move(e)), proj(std::move(p)) {}

  template <typename... Args>
  Result operator()(const int, Args&&... args) const {
    const auto range = iter(std::forward<Args>(args)...);
    for (auto&& value : range) {
      if constexpr (std::is_same_v<std::remove_cvref_t<Proj>, std::identity>) {
        if (Result result = fn(value); !result) return result;
      } else {
        if (Result result = fn(proj(value, std::forward<Args>(args)...)); !result) return result;
      }
    }
    return {};
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

#pragma once

#include "hornetlib/consensus/bips.h"
#include "hornetlib/consensus/types.h"

// Validation algebra for composing Hornet consensus rules as a static tree.
// Nodes either validate the current context, project it, iterate it, or gate a subtree.

namespace hornet::consensus {

// Applies a leaf validator to the current context.
template <typename Fn>
struct Rule {
  Fn fn;
};

template <typename Fn, typename Context>
[[nodiscard]] Result Validate(const Rule<Fn>& node, const Context& context) {
  return node.fn(context);
}

// Runs each child against the same context in sequence, stopping at the first failure.
template <typename... Nodes>
struct All {
  std::tuple<Nodes...> child;
  constexpr explicit All(Nodes... children) : child(children...) {}
};

template<typename... Nodes, typename Context>
[[nodiscard]] Result Validate(const All<Nodes...>& node, const Context& context) {
  Result rv;
  std::apply(
    [&](const auto&... child) {
      ((rv = rv.AndThen([&] { return Validate(child, context); })), ...);
    },
    node.child);
  return rv;
}

// Projects the current context into a child context before validating the child node.
template <typename Proj, typename Child>
struct With {
  Proj projector;
  Child child;
};

template <typename Proj, typename Child, typename Context>
[[nodiscard]] Result Validate(const With<Proj, Child>& node, const Context& context) {
  return Validate(node.child, node.projector(context));
}

struct First {
  template <typename T, typename U>
  decltype(auto) operator()(T&& value, U&&) const { return std::forward<T>(value); }
};

// Validates each element in the current range context with the same child node.
template <typename Child, typename Enum = std::identity, typename Proj = First> 
struct Each {
  Enum enumerate{};
  Proj project{};
  Child child;

  constexpr Each(Child c) : child(std::move(c)) {}
  constexpr Each(Enum e, Child c) : enumerate(std::move(e)), child(std::move(c)) {}
  constexpr Each(Enum e, Proj p, Child c) : enumerate(std::move(e)), project(std::move(p)), child(std::move(c)) {}
};

template <typename Child, typename Enum, typename Proj, typename Context>
[[nodiscard]] Result Validate(const Each<Child, Enum, Proj>& node, const Context& context) {
  for (auto&& obj : node.enumerate(context)) {
    if (Result r = Validate(node.child, node.project(std::forward<decltype(obj)>(obj), context)); !r) return r;
  }
  return {};
}

// Validates the child only when the predicate holds for the current context.
template <typename Pred, typename Child> 
struct When {
  Pred predicate;
  Child child;
};

template <typename Pred, typename Child, typename Context>
[[nodiscard]] Result Validate(const When<Pred, Child>& node, const Context& context) {
  if (node.predicate(context)) return Validate(node.child, context);
  return {};
}

}  // namespace hornet::consensus
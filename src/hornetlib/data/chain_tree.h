#pragma once

#include <span>
#include <variant>
#include <vector>

#include "hornetlib/data/hashed_tree.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/util/assert.h"
#include "hornetlib/util/throw.h"

namespace hornet::data {

template <typename TData>
struct ContextWrapper {
  TData data;
  protocol::Hash hash;
  int height;
};

template <typename TData>
struct DefaultContextPolicy {
  int fork_height;
  std::span<const protocol::Hash> old_chain_hashes;

  using Context = ContextWrapper<TData>;

  const protocol::Hash& GetChainHash(int height) const {
    return old_chain_hashes[height - 1 - fork_height];
  }
  Context Extend(const Context& parent, const TData& next) const {
    return Context{
        .data = next, .hash = GetChainHash(parent.height + 1), .height = parent.height + 1};
  }
  Context Rewind(const Context& child, const TData& prev) const {
    return Context{
        .data = prev, .hash = GetChainHash(child.height - 1), .height = child.height - 1};
  }
};

// Locator is used to resolve an element *either* by height in the main branch,
// *or* by hash in the forest of forks. This locator is transferable between
// ChainTree instances that have the exact same structure and hashes.
using Locator = std::variant<int, protocol::Hash>;

// ChainTree is a data structure that represents a deep, narrow tree by using a hybrid layout: a
// linear array for the main chain combined with a forest for forks near the
// tip. This design is well suited to timechain data, where the vast majority of history is linear,
// but recent forks and reorgs must be handled too. The structure avoids a full hash map and
// minimizes per-node memory usage, making it efficient in both memory use and lookup performance.
template <
    // The data type to store at each position in the main chain and forest.
    typename TData,
    // A richer context type that includes TData, the header hash, block height, and
    // optionally additional metadata. This is stored at each node of the forest.
    typename TContext = ContextWrapper<TData>>
class ChainTree {
 public:
  using Context = TContext;
  template <bool kIsConst>
  class AncestorIterator;
  struct NodeData {
    Context context;
    int root_height;  // Used for pruning the DAG.

    const TData& Data() const {
      return context.data;
    }
    TData& Data() {
      return context.data;
    }
    const protocol::Hash& GetHash() const {
      return context.hash;
    }
    int Height() const {
      return context.height;
    }
  };
  using Forest = HashedTree<NodeData>;
  using ForestIterator = Forest::Iterator;
  using ForestConstIterator = Forest::ConstIterator;
  using Iterator = AncestorIterator<false>;
  using ConstIterator = AncestorIterator<true>;
  using FindResult = std::pair<Iterator, std::optional<Context>>;
  using ConstFindResult = std::pair<ConstIterator, std::optional<Context>>;

  // Public methods
  bool Empty() const {
    return chain_.empty();
  }
  int ChainLength() const {
    return std::ssize(chain_);
  }
  int ChainTipHeight() const {
    return ChainLength() - 1;
  }
  const TData& ChainElement(int height) const {
    Assert(height >= 0 && height < ChainLength());
    return chain_[height];
  }
  Iterator Add(Iterator parent, const Context& context);
  Iterator Find(Locator locator);
  ConstIterator Find(Locator locator) const;
  ConstFindResult FindTipOrForks(const protocol::Hash& hash) const;
  FindResult FindTipOrForks(const protocol::Hash& hash);
  ConstFindResult ChainTip() const;
  FindResult ChainTip();

  // This method performs a chain reorg, i.e. it walks from the given tip node in the forest up
  // to its ancestor fork point in the chain, then swaps the fork's two child branches between
  // the chain and the forest. Returns the updated iterator for the now-invalidated tip.
  Iterator PromoteBranch(Iterator tip, std::span<const protocol::Hash> old_chain_hashes) {
    const int fork_height = GetHeightOfFirstAncestorInChain(tip);
    const DefaultContextPolicy<TData> policy{fork_height, old_chain_hashes};
    return PromoteBranch(tip, policy);
  }

  // In this overload, policy defines an interface for how to compute TContext by extending or
  // rewinding through a linear sequence of TData. This makes ChainTree adaptable to different
  // use cases and metadata schemes. The interface can be seen in DefaultContextPolicy.
  Iterator PromoteBranch(Iterator tip, const auto& policy);

  // Erase an entire subtree, whose common ancestor is the node provided.
  void EraseBranch(Iterator root);

  void PruneForest(int min_height);

  // Navigation
  const TData& GetAncestorAtHeight(ConstIterator tip, int height) const;
  auto AncestorsToHeight(ConstIterator start, int end_height) const;

 protected:
  using ForestNode = Forest::Node;
  int PushToChain(const Context& context);
  ForestIterator AddChild(ForestNode* parent, const Context& context);
  ConstIterator BeginChain(int height) const;
  Iterator BeginChain(int height);
  ConstIterator BeginForest(ForestConstIterator node) const;
  Iterator BeginForest(ForestIterator node);
  int GetHeightOfFirstAncestorInChain(Iterator child) const {
    if (child.InChain()) return child.ChainHeight();
    return child.Node()->data.root_height - 1;
  }

  std::vector<TData> chain_;
  Context chain_tip_context_;
  Forest forest_;
  // The current minimum height among all roots in the tree.
  int min_root_height_ = std::numeric_limits<int>::max();
};

template <typename TData, typename TContext>
inline int ChainTree<TData, TContext>::PushToChain(const Context& context) {
  chain_.push_back(context.data);
  chain_tip_context_ = context;
  return ChainTipHeight();
}

template <typename TData, typename TContext>
inline ChainTree<TData, TContext>::Iterator ChainTree<TData, TContext>::Add(
    Iterator parent,
    /*const protocol::Hash& parent_hash,*/
    const Context& context) {
  // If the parent is invalid, the chain must be empty.
  bool fail = !parent.IsValid() && !chain_.empty();
  // We can only add to one parent location.
  fail |= parent.InChain() && parent.InTree();
  // If parent chain height given it must match context and chain hash.
  fail |= parent.InChain() && ((parent.ChainHeight() != context.height - 1) ||
                               (parent.ChainHeight() >= ChainLength()) /*||
                               (parent_hash != GetChainHash(parent.ChainHeight()))*/);
  // Validate parent in tree.
  if (fail) util::ThrowInvalidArgument("The parent wasn't found or didn't match the requirements.");

  // Now a parent is found in the chain or in the tree.
  if (parent.ChainHeight() == ChainTipHeight())
    return BeginChain(PushToChain(context));
  else
    return BeginForest(AddChild(parent.Node(), context));
}

// This method searches for the hash among the chain tip and the nodes of the forest only.
template <typename TData, typename TContext>
inline ChainTree<TData, TContext>::ConstFindResult ChainTree<TData, TContext>::FindTipOrForks(
    const protocol::Hash& hash) const {
  if (!chain_.empty() && chain_tip_context_.hash == hash)
    return {BeginChain(ChainTipHeight()), chain_tip_context_};

  const ForestConstIterator node = forest_.Find(hash);
  if (forest_.IsValidNode(node)) return {BeginForest(node), node->data.context};

  return {{*this}, std::nullopt};
}

// This method searches for the hash among the chain tip and the nodes of the forest only.
template <typename TData, typename TContext>
inline ChainTree<TData, TContext>::FindResult ChainTree<TData, TContext>::FindTipOrForks(
    const protocol::Hash& hash) {
  if (!chain_.empty() && chain_tip_context_.hash == hash)
    return {BeginChain(ChainTipHeight()), chain_tip_context_};

  const ForestIterator node = forest_.Find(hash);
  if (forest_.IsValidNode(node)) return {BeginForest(node), node->data.context};

  return {{*this}, std::nullopt};
}

template <typename TData, typename TContext>
inline ChainTree<TData, TContext>::Iterator ChainTree<TData, TContext>::Find(Locator locator) {
  if (std::holds_alternative<int>(locator))
    return BeginChain(std::get<int>(locator));
  else
    return BeginForest(forest_.Find(std::get<protocol::Hash>(locator)));
}

template <typename TData, typename TContext>
inline ChainTree<TData, TContext>::ConstIterator ChainTree<TData, TContext>::Find(
    Locator locator) const {
  if (std::holds_alternative<int>(locator)) 
    return BeginChain(std::get<int>(locator));
  else
    return BeginForest(forest_.Find(std::get<protocol::Hash>(locator)));
}

template <typename TData, typename TContext>
inline ChainTree<TData, TContext>::ConstFindResult ChainTree<TData, TContext>::ChainTip() const {
  if (chain_.empty()) return {{*this}, std::nullopt};
  return {BeginChain(ChainTipHeight()), chain_tip_context_};
}

template <typename TData, typename TContext>
inline ChainTree<TData, TContext>::FindResult ChainTree<TData, TContext>::ChainTip() {
  if (chain_.empty()) return {{*this}, std::nullopt};
  return {BeginChain(ChainTipHeight()), chain_tip_context_};
}

// This method performs a chain reorg, i.e. it walks from the given tip node in the forest up
// to its ancestor fork point in the chain, then swaps the fork's two child branches between
// the chain and the forest.
template <typename TData, typename TContext>
inline ChainTree<TData, TContext>::Iterator ChainTree<TData, TContext>::PromoteBranch(
    Iterator tip,
    // In this overload, policy defines an interface to incrementally compute TContext when
    // iterating forward or backward through a linear sequence of TData. This makes ChainTree
    // adaptable to different use cases and metadata schemes. See DefaultContextPolicy.
    const auto& policy) {
  Assert(tip);
  if (tip.InChain()) return tip;  // Nothing to do

  ForestNode* tip_node = tip.Node();
  Assert(forest_.IsLeaf(tip_node));

  // Locate the branch root in the tree.
  std::stack<ForestNode*> stack;
  for (ForestNode& node : forest_.UpFromNode(tip_node)) stack.push(&node);
  const auto root = stack.top();

  // Find the fork point in the chain.
  const Context fork = policy.Rewind(root->data.context, chain_[root->data.Height() - 1]);
  Assert(fork.height < ChainTipHeight());  // The fork point can't be the old tip, by definition.

  // Copy the old chain elements into the tree.
  {
    Context context = fork;
    ForestNode* parent = nullptr;
    for (int height = root->data.Height(); height < ChainLength(); ++height) {
      context = policy.Extend(context, chain_[height]);
      parent = &*AddChild(parent, context);
    }
  }

  // Truncate the chain back to the common ancestor fork point.
  chain_.resize(root->data.Height());
  chain_tip_context_ = fork;

  // Now walk forward down the new branch, moving headers into the heaviest chain.
  for (; !stack.empty(); stack.pop()) PushToChain(stack.top()->data.context);

  // Finally delete the chain containing the new tip from the DAG.
  forest_.EraseChain(tip_node);

  // TODO: Update min_root_height_;

  return ChainTip().first;
}

// Prunes historic branches from the tree.
template <typename TData, typename TContext>
inline void ChainTree<TData, TContext>::PruneForest(int max_keep_depth) {
  const int min_keep_height = ChainTipHeight() - max_keep_depth;
  if (forest_.Empty() || min_root_height_ >= min_keep_height) return;

  min_root_height_ = std::numeric_limits<int>::max();
  const auto range = forest_.ForwardFromOldest();
  for (auto it = range.begin(); it != range.end();) {
    if ((it->data.root_height) < min_keep_height)
      it = forest_.Erase(it);
    else {
      min_root_height_ = std::min<int>(min_root_height_, it->data.root_height);
      ++it;
    }
  }
}

template <typename TData, typename TContext>
inline ChainTree<TData, TContext>::ForestIterator ChainTree<TData, TContext>::AddChild(
    ForestNode* parent, const Context& context) {
  const int root_height = parent != nullptr ? parent->data.root_height : context.height;
  min_root_height_ = std::min(min_root_height_, root_height);
  return forest_.AddChild(parent, {context, root_height});
}

template <typename TData, typename TContext>
inline const TData& ChainTree<TData, TContext>::GetAncestorAtHeight(ConstIterator tip,
                                                                    int height) const {
  if (tip.InChain()) return chain_[height];

  if (tip.Node()->data.root_height > height)
    return chain_[height];
  else {
    for (const auto& node : forest_.UpFromNode(tip.Node()))
      if (node.data.Height() == height) return node.data.Data();
  }
  util::ThrowRuntimeError("Couldn't find an ancestor at height ", height);
}

template <typename TData, typename TContext>
inline ChainTree<TData, TContext>::ConstIterator ChainTree<TData, TContext>::BeginChain(
    int height) const {
  return {*this, std::max(-1, height)};
}

template <typename TData, typename TContext>
inline ChainTree<TData, TContext>::Iterator ChainTree<TData, TContext>::BeginChain(int height) {
  return {*this, std::max(-1, height)};
}

template <typename TData, typename TContext>
inline ChainTree<TData, TContext>::ConstIterator ChainTree<TData, TContext>::BeginForest(
    ForestConstIterator node) const {
  return {*this, forest_.IsValidNode(node) ? &*node : nullptr};
}

template <typename TData, typename TContext>
inline ChainTree<TData, TContext>::Iterator ChainTree<TData, TContext>::BeginForest(
    ForestIterator node) {
  return {*this, forest_.IsValidNode(node) ? &*node : nullptr};
}

template <typename TData, typename TContext>
inline auto ChainTree<TData, TContext>::AncestorsToHeight(ConstIterator start,
                                                          int end_height) const {
  static_assert(std::forward_iterator<Iterator>);
  static_assert(std::sentinel_for<int, Iterator>);
  return std::ranges::subrange{start, BeginChain(end_height)};
}

// Ancestor iterator for walking up from a tip to an exclusive height
template <typename TData, typename TContext>
template <bool kIsConst>
class ChainTree<TData, TContext>::AncestorIterator {
 public:
  // C++20 iterator traits
  using iterator_concept = std::forward_iterator_tag;
  using value_type = TData;
  using pointer = std::conditional_t<kIsConst, const TData, TData>*;
  using reference = std::conditional_t<kIsConst, const TData, TData>&;
  using difference_type = std::ptrdiff_t;

  using NodeType = std::conditional_t<kIsConst, const ForestNode, ForestNode>;
  using ChainTreeType =
      std::conditional_t<kIsConst, const ChainTree<TData, TContext>, ChainTree<TData, TContext>>;

  // Constructors
  AncestorIterator() : chain_tree_(nullptr), node_(), height_(-1) {}
  AncestorIterator(ChainTreeType& chain_tree, NodeType* tip = nullptr, int height = -1)
      : chain_tree_(&chain_tree), node_(tip), height_(height) {}
  AncestorIterator(ChainTreeType& chain_tree, int height)
      : AncestorIterator(chain_tree, nullptr, height) {}
  AncestorIterator(const AncestorIterator& rhs) = default;
  AncestorIterator(AncestorIterator&&) = default;

  // Allow conversion from a non-const iterator to a const iterator.
  template <bool kIsRhsConst>
    requires(kIsConst && !kIsRhsConst)
  AncestorIterator(const AncestorIterator<kIsRhsConst>& rhs)
      : chain_tree_(rhs.chain_tree_), node_(rhs.node_), height_(rhs.height_) {}

  // Default operators
  AncestorIterator& operator=(const AncestorIterator&) = default;
  AncestorIterator& operator=(AncestorIterator&&) = default;
  bool operator!=(const AncestorIterator& rhs) const = default;
  bool operator==(const AncestorIterator& rhs) const = default;

  // Custom operators
  operator bool() const {
    return IsValid();
  }
  reference operator*() const {
    if (InChain())
      return chain_tree_->chain_[height_];
    else if (InTree())
      return node_->data.Data();
    else
      util::ThrowRuntimeError("Tried to access a non-existent element.");
  }
  pointer operator->() const {
    return &operator*();
  }
  AncestorIterator& operator++() {
    if (InTree()) {
      if (node_->parent == nullptr) 
        height_ = node_->data.Height() - 1;
      node_ = node_->parent;
    } else if (InChain())
      --height_;
    return *this;
  }
  AncestorIterator operator++(int) {
    AncestorIterator tmp = *this;
    ++(*this);
    return tmp;
  }

  // Public methods
  std::optional<value_type> TryGet() const {
    if (InChain())
      return chain_tree_->chain_[height_];
    else if (InTree())
      return node_->data.Data();
    else
      return {};
  }
  bool IsValid() const {
    return InChain() || InTree();
  }
  int GetHeight() const {
    return InTree() ? node_->data.Height() : height_;
  }
  NodeType* Node() const {
    return node_;
  }
  Locator MakeLocator(const protocol::Hash& hash) const {
    return InTree() ? hash : height_;
  }
 protected:
  // Protected types and methods are internal to the enclosing ChainTree<TData, TMetadata> class.
  friend ChainTree<TData, TContext>;

  bool InChain() const {
    return height_ >= 0 && height_ < chain_tree_->ChainLength();
  }
  bool InTree() const {
    return node_ != nullptr;
  }
  int ChainHeight() const {
    return height_;
  }

 private:
  // Private data is internal to this class.
  ChainTreeType* chain_tree_;
  NodeType* node_;
  int height_;
};

template <typename TData, typename TContext>
using ImmutableChainTree = ChainTree<const TData, const TContext>;

}  // namespace hornet::data

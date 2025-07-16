#pragma once

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
  using Context = ContextWrapper<TData>;
  static Context Extend(const Context& parent, const TData& next, const protocol::Hash& hash) {
    return Context{.data = next, .hash = hash, .height = parent.height + 1};
  }
  static Context Rewind(const Context& child, const TData& prev, const protocol::Hash& hash) {
    return Context{.data = prev, .hash = hash, .height = child.height - 1};
  }
};

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
    const protocol::Hash& GetHash() const {
      return context.hash;
    }
    int Height() const {
      return context.height;
    }
  };
  using Forest = HashedTree<NodeData>;
  using ForestNode = Forest::Node;
  using Iterator = AncestorIterator<true>;
  using FindResult = std::pair<Iterator, std::optional<Context>>;

  // Public methods
  bool ChainEmpty() const { return chain_.empty(); }
  int ChainLength() const { return std::ssize(chain_); }
  int ChainTipHeight() const {
    return ChainLength() - 1;
  }

  Iterator Add(Iterator parent, const Context& context);
  FindResult FindInTipOrForest(const protocol::Hash& hash);
  FindResult ChainTip() const;

  // This method performs a chain reorg, i.e. it walks from the given tip node in the forest up
  // to its ancestor fork point in the chain, then swaps the fork's two child branches between
  // the chain and the forest.
  void PromoteBranch(Iterator tip, std::span<const protocol::Hash> old_chain_hashes) {
    PromoteBranch(tip, old_chain_hashes, DefaultContextPolicy<TData>{});
  }

  // In this overload, policy defines an interface for how to compute TContext by extending or
  // rewinding through a linear sequence of TData. This makes ChainTree adaptable to different
  // use cases and metadata schemes. The interface can be seen in DefaultContextPolicy.
  void PromoteBranch(Iterator tip, std::span<const protocol::Hash> old_chain_hashes,
                          const auto& policy);

  void PruneForest(int min_height);

  // Navigation
  Iterator BeginChain(int height) const;
  Iterator BeginForest(ForestNode* node) const;
  const TData& GetAncestorAtHeight(Iterator tip, int height) const;
  auto AncestorsToHeight(Iterator start, int end_height) const;

 protected:
  using ForestIterator = Forest::Iterator;
  int PushToChain(const Context& context);
  ForestNode* AddChild(ForestNode* parent, const Context& context);
  Iterator BeginForest(ForestIterator node) const;

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
  fail |= parent.InTree() /* && parent_hash != parent.Node()->hash */;
  if (fail) util::ThrowInvalidArgument("The parent wasn't found or didn't match the requirements.");

  // Now a parent is found in the chain or in the tree.
  if (parent.ChainHeight() == ChainTipHeight())
    return BeginChain(PushToChain(context));
  else
    return BeginForest(AddChild(parent.Node(), context));
}

// This method searches for the hash among the chain tip and the nodes of the forest only.
template <typename TData, typename TContext>
inline ChainTree<TData, TContext>::FindResult ChainTree<TData, TContext>::FindInTipOrForest(
    const protocol::Hash& hash) {
  if (!chain_.empty() && chain_tip_context_.hash == hash)
    return {BeginChain(ChainTipHeight()), chain_tip_context_};

  const ForestIterator node = forest_.Find(hash);
  if (forest_.IsValidNode(node)) return {BeginForest(node), node->data.context};

  return {{*this}, std::nullopt};
}

template <typename TData, typename TContext>
inline ChainTree<TData, TContext>::FindResult ChainTree<TData, TContext>::ChainTip() const {
  if (chain_.empty()) return {{*this}, std::nullopt};
  return {BeginChain(ChainTipHeight()), chain_tip_context_};
}

// This method performs a chain reorg, i.e. it walks from the given tip node in the forest up
// to its ancestor fork point in the chain, then swaps the fork's two child branches between
// the chain and the forest.
template <typename TData, typename TContext>
inline void ChainTree<TData, TContext>::PromoteBranch(
    Iterator tip,
    // old_chain_hashes must include exactly one hash per element to be moved into the DAG.
    std::span<const protocol::Hash> old_chain_hashes,
    // In this overload, policy defines an interface to incrementally compute TContext when 
    // iterating forward or backward through a linear sequence of TData. This makes ChainTree 
    // adaptable to different use cases and metadata schemes. See DefaultContextPolicy.
    const auto& policy) {
  Assert(tip.InTree());
  ForestNode* tip_node = tip.Node();
  Assert(tip_node != nullptr);
  Assert(forest_.IsLeaf(tip_node));

  // Locate the branch root in the tree.
  std::stack<ForestNode*> stack;
  for (ForestNode& node : forest_.UpFromNode(tip_node)) stack.push(&node);
  const auto root = stack.top();

  // Find the fork point in the chain.
  const Context fork =
      policy.Rewind(root->data.context, chain_[root->data.Height() - 1], {});
  Assert(fork.height < ChainTipHeight());  // The fork point can't be the old tip, by definition.
  Assert(std::ssize(old_chain_hashes) == ChainTipHeight() - fork.height);

  // Copy the old chain elements into the tree.
  {
    Context context = fork;
    ForestNode* parent = nullptr;
    for (int height = root->data.Height(); height < ChainLength(); ++height) {
      context = policy.Extend(context, chain_[height],
                                       old_chain_hashes[height - root->data.Height()]);
      parent = AddChild(parent, context);
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
inline ChainTree<TData, TContext>::ForestNode* ChainTree<TData, TContext>::AddChild(
    ForestNode* parent, const Context& context) {
  const int root_height = parent != nullptr ? parent->data.root_height : context.height;
  min_root_height_ = std::min(min_root_height_, root_height);
  const auto it = forest_.AddChild(parent, {context, root_height});
  return forest_.IsValidNode(it) ? &*it : nullptr;
}

template <typename TData, typename TContext>
inline const TData& ChainTree<TData, TContext>::GetAncestorAtHeight(Iterator tip,
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
inline ChainTree<TData, TContext>::Iterator ChainTree<TData, TContext>::BeginChain(int height) const {
  return {*this, std::max(-1, height)};
}

template <typename TData, typename TContext>
inline ChainTree<TData, TContext>::Iterator ChainTree<TData, TContext>::BeginForest(ForestIterator node) const {
  return {*this, forest_.IsValidNode(node) ? &*node : nullptr};
}

template <typename TData, typename TContext>
inline ChainTree<TData, TContext>::Iterator ChainTree<TData, TContext>::BeginForest(ForestNode* node) const {
  return {*this, node};
}

template <typename TData, typename TContext>
inline auto ChainTree<TData, TContext>::AncestorsToHeight(Iterator start, int end_height) const {
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

  // Constructors
  AncestorIterator() : chain_tree_(nullptr), node_(), height_(-1) {}
  AncestorIterator(const ChainTree<TData, TContext>& chain_tree, ForestNode* tip = nullptr,
                   int height = -1)
      : chain_tree_(&chain_tree), node_(tip), height_(height) {}
  AncestorIterator(const ChainTree<TData, TContext>& chain_tree, int height)
      : AncestorIterator(chain_tree, nullptr, height) {}
  AncestorIterator(const AncestorIterator& rhs) = default;
  AncestorIterator(AncestorIterator&&) = default;

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
      if (node_->parent != nullptr) height_ = node_->data.Height();
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

 protected:
  // Protected types and methods are internal to the enclosing ChainTree<TData, TMetadata> class.
  friend ChainTree<TData, TContext>;

  bool InChain() const {
    return height_ >= 0;
  }
  bool InTree() const {
    return node_ != nullptr;
  }
  int ChainHeight() const {
    return height_;
  }
  ForestNode* Node() const {
    return node_;
  }

 private:
  // Private data is internal to this class.
  const ChainTree<TData, TContext>* chain_tree_;
  ForestNode* node_;
  int height_;
};

}  // namespace hornet::data

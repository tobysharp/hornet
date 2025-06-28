// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/data/hashed_tree.h"

#include <cstdint>

#include <gtest/gtest.h>

#include "hornetlib/util/assert.h"

namespace hornet::data {
namespace {

struct Dummy {
  int id;
  uint32_t hash;
  explicit Dummy(int id_val = 0) : id(id_val), hash(static_cast<uint32_t>(id_val)) {}
};

struct DummyHasher {
  uint32_t operator()(const Dummy& d) const { return d.hash; }
};

using DummyTree = HashedTree<Dummy, DummyHasher>;

TEST(HashedTreeTest, AddAndFind) {
  DummyTree tree{DummyHasher{}};
  auto root = tree.AddChild(nullptr, Dummy{1});
  auto child = tree.AddChild(&*root, Dummy{2});
  auto other_root = tree.AddChild(nullptr, Dummy{3});

  ASSERT_TRUE(tree.IsValidNode(root));
  ASSERT_TRUE(tree.IsValidNode(child));
  ASSERT_TRUE(tree.IsValidNode(other_root));

  EXPECT_EQ(tree.Find(root->hash), root);
  EXPECT_EQ(tree.Find(child->hash), child);
  EXPECT_EQ(tree.Find(other_root->hash), other_root);

  EXPECT_FALSE(tree.IsLeaf(&*root));
  EXPECT_TRUE(tree.IsLeaf(&*child));
  EXPECT_TRUE(tree.IsLeaf(&*other_root));
}

TEST(HashedTreeTest, ErasePromotesChildren) {
  DummyTree tree{DummyHasher{}};
  auto root = tree.AddChild(nullptr, Dummy{10});
  auto child1 = tree.AddChild(&*root, Dummy{20});
  auto child2 = tree.AddChild(&*root, Dummy{30});
  auto grandchild = tree.AddChild(&*child1, Dummy{40});

  auto next_it = tree.Erase(child1);

  // grandchild should now be a root node
  auto gc_it = tree.Find(grandchild->hash);
  ASSERT_TRUE(tree.IsValidNode(gc_it));
  EXPECT_EQ(gc_it->parent, nullptr);

  // iterator returned should point to child2
  ASSERT_TRUE(tree.IsValidNode(next_it));
  EXPECT_EQ(&*next_it, &*child2);

  // root should still have child2 as descendant
  EXPECT_FALSE(tree.IsLeaf(&*root));

  // erased node should not be found
  EXPECT_FALSE(tree.IsValidNode(tree.Find(child1->hash)));
}

TEST(HashedTreeTest, EraseChainRemovesAncestors) {
  DummyTree tree{DummyHasher{}};
  auto root1 = tree.AddChild(nullptr, Dummy{1});
  auto child1 = tree.AddChild(&*root1, Dummy{2});
  auto grandchild1 = tree.AddChild(&*child1, Dummy{3});
  auto sibling = tree.AddChild(&*root1, Dummy{4});
  auto other_root = tree.AddChild(nullptr, Dummy{5});

  tree.EraseChain(&*grandchild1);

  // Removed nodes should not be found
  EXPECT_FALSE(tree.IsValidNode(tree.Find(root1->hash)));
  EXPECT_FALSE(tree.IsValidNode(tree.Find(child1->hash)));
  EXPECT_FALSE(tree.IsValidNode(tree.Find(grandchild1->hash)));

  // sibling should now be a root
  auto sib_it = tree.Find(sibling->hash);
  ASSERT_TRUE(tree.IsValidNode(sib_it));
  EXPECT_EQ(sib_it->parent, nullptr);

  // other_root remains unaffected
  EXPECT_TRUE(tree.IsValidNode(tree.Find(other_root->hash)));
}

}  // namespace
}  // namespace hornet::data

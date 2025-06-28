// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/util/pointer_iterator.h"

#include <gtest/gtest.h>

namespace hornet::util {
namespace {

struct Node {
  int value;
  Node* next;
};

struct GetNext {
  Node* operator()(Node* n) const {
    return n->next;
  }
  const Node* operator()(const Node* n) const {
    return n->next;
  }
};

using Iterator = PointerIterator<Node, GetNext, false>;
using ConstIterator = PointerIterator<Node, GetNext, true>;

TEST(PointerIteratorTest, DefaultConstructedIsNull) {
  Iterator it;
  EXPECT_EQ(it, nullptr);
}

TEST(PointerIteratorTest, DereferenceAndIncrement) {
  Node n3{3, nullptr};
  Node n2{2, &n3};
  Node n1{1, &n2};

  Iterator it(&n1);
  EXPECT_EQ(it->value, 1);
  EXPECT_EQ((*it).value, 1);

  ++it;
  EXPECT_EQ(it->value, 2);

  Iterator old = it++;
  EXPECT_EQ(old->value, 2);
  EXPECT_EQ(it->value, 3);

  ++it;
  EXPECT_EQ(it, nullptr);
}

TEST(PointerIteratorTest, EqualityWithOtherIterator) {
  Node node{5, nullptr};
  Iterator a(&node);
  Iterator b(&node);
  EXPECT_TRUE(a == b);
  ++a;
  EXPECT_FALSE(a == b);
}

TEST(PointerIteratorTest, ConstructConstIteratorFromPointer) {
  Node node{7, nullptr};
  ConstIterator cit(&node);
  EXPECT_EQ(cit->value, 7);
  ++cit;
  EXPECT_EQ(cit, nullptr);
}

}  // namespace
}  // namespace hornet::util

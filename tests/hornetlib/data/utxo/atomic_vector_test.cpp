#include "hornetlib/data/utxo/atomic_vector.h"

#include <gtest/gtest.h>

namespace hornet::data::utxo {
namespace {

struct Item {
  int value;
  explicit Item(int v) : value(v) {}
  bool operator==(const Item& other) const noexcept { return value == other.value; }
};

TEST(AtomicVectorTest, InitiallyEmpty) {
  AtomicVector<Item> vec;
  EXPECT_TRUE(vec.Empty());
  EXPECT_EQ(vec.Size(), 0);
  EXPECT_EQ(vec.Snapshot()->size(), 0);
}

TEST(AtomicVectorTest, EmplaceBackAddsElements) {
  AtomicVector<Item> vec;

  vec.EmplaceBack(Item{1});
  vec.EmplaceBack(Item{2});
  vec.EmplaceBack(Item{3});

  EXPECT_FALSE(vec.Empty());
  EXPECT_EQ(vec.Size(), 3);
  EXPECT_EQ((*vec[0]).value, 1);
  EXPECT_EQ((*vec[1]).value, 2);
  EXPECT_EQ((*vec[2]).value, 3);
}

TEST(AtomicVectorTest, CopyCreatesDeepCopy) {
  AtomicVector<Item> vec;
  vec.EmplaceBack(Item{42});

  auto copy = vec.Copy();
  EXPECT_EQ(copy->size(), 1);
  EXPECT_EQ(copy->at(0)->value, 42);

  // Modifying the copy does not affect the original
  copy->clear();
  EXPECT_TRUE(copy->empty());
  EXPECT_EQ(vec.Size(), 1);
}

TEST(AtomicVectorTest, EditPublishesChanges) {
  AtomicVector<Item> vec;
  {
    auto writer = vec.Edit();
    writer->push_back(std::make_shared<Item>(7));
    writer->push_back(std::make_shared<Item>(9));
  }
  EXPECT_EQ(vec.Size(), 2);
  EXPECT_EQ((*vec[0]).value, 7);
  EXPECT_EQ((*vec[1]).value, 9);
}

TEST(AtomicVectorTest, EraseFrontRemovesExpectedElements) {
  AtomicVector<Item> vec;
  for (int i = 0; i < 5; ++i) vec.EmplaceBack(Item{i});

  vec.EraseFront(2);
  EXPECT_EQ(vec.Size(), 3);
  EXPECT_EQ((*vec[0]).value, 2);

  vec.EraseFront(10);  // larger than size
  EXPECT_TRUE(vec.Empty());

  vec.EraseFront(0);   // no-op
  EXPECT_TRUE(vec.Empty());
}

TEST(AtomicVectorTest, EraseBackRemovesExpectedElements) {
  AtomicVector<Item> vec;
  for (int i = 0; i < 5; ++i) vec.EmplaceBack(Item{i});

  vec.EraseBack(2);
  EXPECT_EQ(vec.Size(), 3);
  EXPECT_EQ((*vec[2]).value, 2);

  vec.EraseBack(10);  // larger than size
  EXPECT_TRUE(vec.Empty());

  vec.EraseBack(0);   // no-op
  EXPECT_TRUE(vec.Empty());
}

TEST(AtomicVectorTest, OperatorIndexReturnsCorrectSharedPtr) {
  AtomicVector<Item> vec;
  vec.EmplaceBack(Item{10});
  vec.EmplaceBack(Item{20});
  auto ptr = vec[1];
  EXPECT_EQ(ptr->value, 20);
  EXPECT_EQ(vec[0]->value, 10);
}

TEST(AtomicVectorTest, SnapshotReflectsLatestState) {
  AtomicVector<Item> vec;
  {
    auto writer = vec.Edit();
    writer->push_back(std::make_shared<Item>(5));
  }
  auto snap1 = vec.Snapshot();
  EXPECT_EQ(snap1->size(), 1);
  EXPECT_EQ(snap1->at(0)->value, 5);

  vec.EmplaceBack(Item{9});
  auto snap2 = vec.Snapshot();
  EXPECT_EQ(snap2->size(), 2);
  EXPECT_EQ(snap2->at(1)->value, 9);
}

}  // namespace
}  // namespace hornet::data::utxo

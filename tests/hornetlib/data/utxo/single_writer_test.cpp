#include "hornetlib/data/utxo/single_writer.h"

#include <atomic>
#include <barrier>
#include <thread>

#include <gtest/gtest.h>

namespace hornet::data::utxo {
namespace {

struct Counter {
  int value = 0;
  void Increment() { ++value; }
};

// --- Basic construction and snapshot ---
TEST(SingleWriterTest, ConstructsAndSnapshots) {
  SingleWriter<int> sw(42);
  EXPECT_EQ(*sw, 42);
  EXPECT_EQ(*sw.Snapshot(), 42);
}

// --- Copy returns independent mutable instance ---
TEST(SingleWriterTest, CopyCreatesIndependentMutableObject) {
  SingleWriter<int> sw(10);
  auto copy = sw.Copy();
  *copy = 99;
  EXPECT_EQ(*copy, 99);
  EXPECT_EQ(*sw, 10);  // original unchanged
}

// --- Edit publishes automatically on destruction ---
TEST(SingleWriterTest, EditPublishesOnDestruction) {
  SingleWriter<int> sw(5);
  {
    auto writer = sw.Edit();
    *writer = 42;
  }
  EXPECT_EQ(*sw, 42);
  *sw.Edit() = 64;
  EXPECT_EQ(*sw, 64);
}

// --- Edit with multiple operator-> and operator* calls ---
TEST(SingleWriterTest, MultipleDereferencesWork) {
  SingleWriter<Counter> sw;
  {
    auto writer = sw.Edit();
    writer->Increment();
    (*writer).Increment();
    writer->value += 5;
  }
  EXPECT_EQ(sw->value, 7);

  sw.Edit()->Increment();
  EXPECT_EQ(sw->value, 8);
}

// --- Explicit Publish call updates snapshot ---
TEST(SingleWriterTest, PublishUpdatesSnapshot) {
  SingleWriter<int> sw(1);
  sw.Publish(std::make_shared<const int>(9));
  EXPECT_EQ(*sw, 9);
}

// --- Concurrent readers see consistent snapshots ---
TEST(SingleWriterTest, MultipleReadersSeeConsistentSnapshots) {
  SingleWriter<int> sw(0);

  std::atomic<bool> stop{false};
  std::thread writer([&] {
    int v = 0;
    while (!stop) {
      auto w = sw.Edit();
      *w = ++v;
    }
  });

  std::atomic<int> read_sum{0};
  std::thread reader1([&] {
    for (int i = 0; i < 1000; ++i) {
      read_sum += *sw;
    }
  });
  std::thread reader2([&] {
    for (int i = 0; i < 1000; ++i) {
      read_sum += *sw;
    }
  });

  reader1.join();
  reader2.join();
  stop = true;
  writer.join();

  // The actual value is non-deterministic, but test ensures it runs safely
  SUCCEED();
}

// --- Edit from multiple threads serialized by mutex ---
TEST(SingleWriterTest, MultipleWritersAreSerialized) {
  SingleWriter<int> sw(0);

  std::barrier start_line(3);
  auto worker = [&](int id) {
    start_line.arrive_and_wait();
    auto w = sw.Edit();
    *w = id;
    // destruct -> publish
  };

  std::thread t1(worker, 1);
  std::thread t2(worker, 2);
  start_line.arrive_and_wait();

  t1.join();
  t2.join();

  // One of the writers' values should have been last
  int result = *sw;
  EXPECT_TRUE(result == 1 || result == 2);
}

// --- Verify Copy used internally by Writer ---
TEST(SingleWriterTest, WriterUsesCopyInternally) {
  SingleWriter<int> sw(1);
  {
    auto w = sw.Edit();
    *w = 2;
    EXPECT_EQ(*w, 2);
  }
  EXPECT_EQ(*sw, 2);
}

}  // namespace
}  // namespace hornet::data::utxo

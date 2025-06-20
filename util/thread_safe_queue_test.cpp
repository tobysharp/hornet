#include "util/thread_safe_queue.h"
#include "util/timeout.h"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>

namespace hornet::util {
namespace {

TEST(ThreadSafeQueueTest, PushAndTryPop) {
  ThreadSafeQueue<int> q;
  EXPECT_TRUE(q.Empty());

  q.Push(1);
  q.Push(2);
  EXPECT_EQ(q.Size(), 2);

  auto first = q.TryPop();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(*first, 1);

  auto second = q.TryPop();
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*second, 2);

  EXPECT_TRUE(q.Empty());
}

TEST(ThreadSafeQueueTest, WaitPopBlocksUntilPush) {
  ThreadSafeQueue<int> q;
  std::optional<int> result;
  std::thread t([&] { result = q.WaitPop(); });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  q.Push(42);
  t.join();

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 42);
}

TEST(ThreadSafeQueueTest, WaitPopTimeout) {
  ThreadSafeQueue<int> q;
  auto val = q.WaitPop(Timeout(10));
  EXPECT_FALSE(val.has_value());
}

TEST(ThreadSafeQueueTest, StopUnblocksWait) {
  ThreadSafeQueue<int> q;
  std::optional<int> val{0};
  std::thread t([&] { val = q.WaitPop(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  q.Stop();
  t.join();
  EXPECT_FALSE(val.has_value());
  EXPECT_TRUE(q.IsStopped());
}

TEST(ThreadSafeQueueTest, StartAfterStop) {
  ThreadSafeQueue<int> q;
  q.Stop();
  q.Start();

  q.Push(7);
  auto val = q.WaitPop(Timeout(50));
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, 7);
}

TEST(ThreadSafeQueueTest, EraseIfAndClear) {
  ThreadSafeQueue<int> q;
  q.Push(1);
  q.Push(2);
  q.Push(3);

  q.EraseIf([](int v) { return v % 2 == 1; });
  EXPECT_EQ(q.Size(), 1);
  auto val = q.TryPop();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, 2);

  q.Push(4);
  q.Push(5);
  q.Clear();
  EXPECT_TRUE(q.Empty());
}

}  // namespace
}  // namespace hornet::util


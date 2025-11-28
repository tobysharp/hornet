// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/data/priority_shared_mutex.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace hornet::data {
namespace {

using namespace std::chrono_literals;

class PrioritySharedMutexTest : public ::testing::Test {
 protected:
  PrioritySharedMutex mutex_;
};

TEST_F(PrioritySharedMutexTest, BasicLockUnlock) {
  mutex_.lock();
  mutex_.unlock();
}

TEST_F(PrioritySharedMutexTest, BasicSharedLockUnlock) {
  mutex_.lock_shared();
  mutex_.unlock_shared();
}

TEST_F(PrioritySharedMutexTest, MultipleReaders) {
  std::atomic<int> active_readers{0};
  std::vector<std::thread> threads;
  
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&] {
      mutex_.lock_shared();
      active_readers++;
      std::this_thread::sleep_for(10ms);
      active_readers--;
      mutex_.unlock_shared();
    });
  }

  for (auto& t : threads) t.join();
  EXPECT_EQ(active_readers, 0);
}

TEST_F(PrioritySharedMutexTest, WriterExcludesReaders) {
  mutex_.lock();
  
  std::atomic<bool> reader_started{false};
  std::atomic<bool> reader_acquired{false};
  
  std::thread t([&] {
    reader_started = true;
    mutex_.lock_shared(); // Should block
    reader_acquired = true;
    mutex_.unlock_shared();
  });

  // Give thread time to start and block
  while(!reader_started) std::this_thread::yield();
  std::this_thread::sleep_for(50ms);
  
  EXPECT_FALSE(reader_acquired);
  
  mutex_.unlock();
  t.join();
  EXPECT_TRUE(reader_acquired);
}

TEST_F(PrioritySharedMutexTest, WritePreference) {
  // This test verifies that a waiting writer prevents new readers from acquiring the lock.
  
  std::atomic<bool> t1_acquired{false};
  std::atomic<bool> t2_waiting{false};
  std::atomic<bool> t2_acquired{false};
  std::atomic<bool> t3_acquired{false};

  // 1. Start a reader (t1) that holds the lock for a while.
  std::thread t1([&] {
    mutex_.lock_shared();
    t1_acquired = true;
    
    // Wait until t2 is definitely waiting
    while (!t2_waiting) std::this_thread::yield();
    // Wait a bit more to ensure t3 has had a chance to try and fail
    std::this_thread::sleep_for(100ms);
    
    mutex_.unlock_shared();
  });

  while (!t1_acquired) std::this_thread::yield();

  // 2. Start a writer (t2). It should block because t1 holds the lock.
  std::thread t2([&] {
    t2_waiting = true;
    mutex_.lock();
    t2_acquired = true;
    // When we get here, t1 is done.
    // t3 should NOT have acquired the lock yet if write preference is working.
    EXPECT_FALSE(t3_acquired);
    std::this_thread::sleep_for(50ms);
    mutex_.unlock();
  });

  // Give t2 time to block
  std::this_thread::sleep_for(20ms);

  // 3. Start another reader (t3). 
  // With a standard shared_mutex, t3 might jump the queue and share with t1.
  // With PrioritySharedMutex, t3 must wait because t2 is waiting.
  std::thread t3([&] {
    mutex_.lock_shared();
    t3_acquired = true;
    mutex_.unlock_shared();
  });

  t1.join();
  t2.join();
  t3.join();
  
  EXPECT_TRUE(t2_acquired);
  EXPECT_TRUE(t3_acquired);
}

TEST_F(PrioritySharedMutexTest, WriteReentrancy) {
  mutex_.lock();
  // Should not deadlock
  mutex_.lock();
  mutex_.unlock();
  mutex_.unlock();
}

} // namespace
} // namespace hornet::data

// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

#include "hornetlib/util/timeout.h"

namespace hornet::util {

template <typename T>
class ThreadSafeQueue {
 public:
  void SetMaxSize(int size) {
    {
      std::scoped_lock lock{mutex_};
      max_size_ = size;
    }
    cv_full_.notify_all();
  }

  bool Push(T item) {
    {
      std::unique_lock lock{mutex_};
      cv_full_.wait(lock, [&] { return is_stopped_ || std::ssize(queue_) < max_size_; });
      if (is_stopped_) return false;
      queue_.emplace_back(std::move(item));
    }
    cv_empty_.notify_one();
    return true;
  }

  std::optional<T> TryPop() {
    std::optional<T> item;
    {
      std::scoped_lock lock{mutex_};
      if (queue_.empty()) return {};
      item = std::move(queue_.front());
      queue_.pop_front();
    }
    cv_full_.notify_one();
    return item;
  }

  std::optional<T> WaitPop(const Timeout& timeout = Timeout::Infinite(), bool drain_on_stop = false) {
    std::optional<T> item;
    {
      std::unique_lock lock{mutex_};
      if (timeout.IsInfinite())
        cv_empty_.wait(lock, [&] { return is_stopped_ || !queue_.empty(); });
      else if (!cv_empty_.wait_for(lock, timeout.RemainingMs(), [&] { return is_stopped_ || !queue_.empty(); }))
        return {};  // Timeout

      if ((!drain_on_stop && is_stopped_) || queue_.empty()) return {};

      item = std::move(queue_.front());
      queue_.pop_front();
    }
    cv_full_.notify_one();
    return item;
  }

  template <typename Pred>
  void EraseIf(Pred&& predicate) {
    {
      std::scoped_lock lock{mutex_};
      std::erase_if(queue_, predicate);
    }
    cv_full_.notify_all();
  }

  bool Empty() const {
    std::scoped_lock lock{mutex_};
    return queue_.empty();
  }

  int Size() const {
    std::scoped_lock lock{mutex_};
    return std::ssize(queue_);
  }

  bool IsStopped() const {
    std::scoped_lock lock{mutex_};
    return is_stopped_;
  }

  void Stop() {
    {
      std::scoped_lock lock{mutex_};
      is_stopped_ = true;
    }
    cv_empty_.notify_all();
    cv_full_.notify_all();
  }

  void Start() {
    std::scoped_lock lock{mutex_};
    is_stopped_ = false;
  }

  void Clear() {
    {
      std::scoped_lock lock{mutex_};
      queue_.clear();
    }
    cv_full_.notify_all();
  }

 private:
  int max_size_ = std::numeric_limits<int>::max();
  bool is_stopped_ = false;
  std::deque<T> queue_;
  mutable std::mutex mutex_;
  std::condition_variable cv_empty_;        
  std::condition_variable cv_full_;
};

}  // namespace hornet::util

#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

#include "util/timeout.h"

namespace hornet::util {

template <typename T>
class ThreadSafeQueue {
 public:
  void Push(T item) {
    {
        const std::scoped_lock{mutex_};
        queue_.emplace_back(std::move(item));
    }
    cv_.notify_one();
  }

  std::optional<T> TryPop() {
    const std::scoped_lock{mutex_};
    if (queue_.empty()) return {};
    const T item = std::move(queue_.front());
    queue_.pop_front();
    return item;
  }

  std::optional<T> WaitPop(const Timeout& timeout = Timeout::Infinite()) {
    std::scoped_lock lock{mutex_};
    if (timeout.IsInfinite()) {
        cv_.wait(lock, [&] { return !queue_.empty(); });
    } else {
        if (!cv_.wait_for(lock, timeout.RemainingMs(), [&] { return !queue_.empty(); })) {
            return {};  // Timeout
        }
    }
    const T item = std::move(queue_.front());
    queue_.pop_front();
    return item;
  }

  bool Empty() const {
    return queue_.empty();
  }

  int Size() const {
    return std::ssize(queue_);
  }

  void Clear() {
    {
        const std::scoped_lock{mutex_};
        queue_.clear();
    }
    cv_.notify_all();
  }

 private:
  std::deque<T> queue_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
};

}  // namespace hornet::util

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
        std::scoped_lock lock{mutex_};
        queue_.emplace_back(std::move(item));
    }
    cv_.notify_one();
  }

  std::optional<T> TryPop() {
    std::scoped_lock lock{mutex_};
    if (queue_.empty()) return {};
    const T item = std::move(queue_.front());
    queue_.pop_front();
    return item;
  }

  std::optional<T> WaitPop(const Timeout& timeout = Timeout::Infinite()) {
    std::unique_lock lock{mutex_};
    if (timeout.IsInfinite()) {
        cv_.wait(lock, [&] { return is_stopped_ || !queue_.empty(); });
    } else {
        if (!cv_.wait_for(lock, timeout.RemainingMs(), [&] { return is_stopped_ || !queue_.empty(); })) {
            return {};  // Timeout
        }
    }
    if (!is_stopped_ && !queue_.empty()) {
      const T item = std::move(queue_.front());
      queue_.pop_front();
      return item;
    }
    return {};
  }

  template <typename Pred>
  void EraseIf(Pred&& predicate) {
    std::remove_if(queue_.begin(), queue_.end(), predicate);
  }

  bool Empty() const {
    return queue_.empty();
  }

  int Size() const {
    return std::ssize(queue_);
  }

  bool IsStopped() const {
    return is_stopped_;
  }

  void Stop() {
    is_stopped_ = true;
    cv_.notify_all();
  }

  void Start() {
    is_stopped_ = false;
  }

  void Clear() {
    std::scoped_lock lock{mutex_};
    queue_.clear();
  }

 private:
  std::atomic<bool> is_stopped_ = false;
  std::deque<T> queue_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
};

}  // namespace hornet::util

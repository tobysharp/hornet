#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "hornetlib/util/assert.h"

namespace hornet::data::utxo {

class Compacter {
 public:
  using MergeFn = std::function<void(int)>;

  Compacter(int num_threads, MergeFn merge) : merge_(std::move(merge)), abort_(false), paused_(false), running_(0) {
    for (int i = 0; i < num_threads; ++i)
      threads_.emplace_back([this] { Run(); });
  }

  ~Compacter() {
    abort_ = true;
    cv_.notify_all();
    for (auto& thread : threads_)
      if (thread.joinable()) thread.join();
  }

  void Enqueue(int level) {
    std::lock_guard lk(mu_);
    ready_.insert(level);
    cv_.notify_one();
  }

  void Pause() {
    paused_ = true;
    int current = running_;
    while (current != 0) {
      running_.wait(current);
      current = running_;
    }
  }

  void Resume() {
    Assert(running_ == 0);
    paused_ = false;
    cv_.notify_all();
  }

  struct Guard {
    Guard(Compacter& obj) : obj_(obj) { obj_.Pause(); }
    ~Guard() { obj_.Resume(); }
    Compacter& obj_;
  };

  [[nodiscard]] Guard Lock() { return *this; }

 private:
  std::optional<int> Pop() {
    std::unique_lock lk(mu_);
    cv_.wait(lk, [&]{ return abort_ || (!paused_ && !ready_.empty()); });
    if (abort_) return std::nullopt;
    const auto first = ready_.begin();
    const int rv = *first;
    ready_.erase(first);
    return rv;
  }

  void Run() {
    while (auto opt = Pop()) {
      ++running_;

      if (!paused_) merge_(*opt);
      else Enqueue(*opt);
        
      --running_;
      running_.notify_all();
    }
  }

  MergeFn merge_;

  std::mutex mu_;
  std::atomic<bool> abort_;
  std::atomic<bool> paused_;
  std::atomic<int> running_;
  std::condition_variable cv_;
  std::set<int> ready_;

  std::vector<std::thread> threads_;
};

}  // namespace hornet::data::utxo

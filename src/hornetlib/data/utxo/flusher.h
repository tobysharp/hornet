#pragma once

#include <atomic>
#include <functional>
#include <thread>

namespace hornet::data::utxo {

class Flusher {
 public:
  using CommitFn = std::function<void(int)>;

  Flusher(CommitFn commit) : commit_(std::move(commit)), height_(kIdle), thread_{[this] { Run(); }} {
  }

  ~Flusher() {
    Abort();
    if (thread_.joinable()) thread_.join();
  }

  void Abort() {
    height_ = kAbort;
    height_.notify_all();
  }

  void Enqueue(int height) {
    int old = height_;
    while (old < height && !height_.compare_exchange_weak(old, height));
    if (old < height_) height_.notify_one();
  }

 private:
  int Pop() noexcept {
    int value = height_;
    while (value == kIdle) {
      height_.wait(kIdle);  // Sleep until height_ changes or explicitly signaled.
      value = height_;
    }
    height_ = kIdle;
    return value;
  }

  void Run() {
    while (true) {
      const int height = Pop();
      if (height == kAbort) break;
      commit_(height);
    }
  }

  // Sentinel values
  enum : int { kIdle = -1, kAbort = -2 };

  CommitFn commit_;
  std::atomic<int> height_;
  std::thread thread_;
};

}  // namespace hornet::data::utxo

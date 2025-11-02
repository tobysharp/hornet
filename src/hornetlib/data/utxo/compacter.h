#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace hornet::data::utxo {

class Compacter {
 public:
  using MergeFn = std::function<void(int)>;

  Compacter(int num_threads, MergeFn merge) : merge_(std::move(merge)), abort_(false) {
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

 private:
  std::optional<int> Pop() {
    std::unique_lock lk(mu_);
    cv_.wait(lk, [&]{ return abort_ || !ready_.empty(); });
    if (abort_) return std::nullopt;
    const auto first = ready_.begin();
    const int rv = *first;
    ready_.erase(first);
    return rv;
  }

  void Run() {
    while (auto opt = Pop()) merge_(*opt);
  }

  MergeFn merge_;

  std::mutex mu_;
  std::atomic_bool abort_;
  std::condition_variable cv_;
  std::set<int> ready_;

  std::vector<std::thread> threads_;
};

}  // namespace hornet::data::utxo

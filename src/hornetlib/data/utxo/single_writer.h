#pragma once

#include <atomic>
#include <memory>
#include <mutex>

namespace hornet::data::utxo {

// Synchronization wrapper supporting multiple lock-free readers and serialized, scoped edits for writers.
template <class T>
class SingleWriter {
 public:
  class Writer {
   public:
    Writer(SingleWriter<T>& target) noexcept : target_(target), lock_(target.mutex_) {}
    ~Writer() noexcept {
      if (copy_) target_.Publish(copy_);
    }
    T* operator->() { return EnsureCopy(); }
    T& operator*() { return *EnsureCopy(); }

   private:
    T* EnsureCopy() {
      if (!copy_) copy_ = std::make_shared<T>(*target_.Snapshot());
      return copy_.get();
    }

    SingleWriter<T>& target_;
    std::unique_lock<std::mutex> lock_;
    std::shared_ptr<T> copy_;
  };

  template <typename... Args>
  SingleWriter(Args&&... args) : ptr_(std::make_shared<T>(std::forward<Args>(args)...)) {}
  SingleWriter(std::shared_ptr<const T> ptr) : ptr_(std::move(ptr)) {}

  // Returns a read-only snapshot of the current state.
  std::shared_ptr<const T> Snapshot() const { return ptr_; }

  // Operates on a read-only snapshot of the current state.
  const T* operator ->() const { return Snapshot().get(); }
  const T& operator *() const { return *Snapshot(); }

  // Atomically publishes a new version, ignoring other writers that may be mutating snapshots.
  void Publish(std::shared_ptr<const T> ptr) { ptr_ = std::move(ptr); }

  // Returns an object that holds an exclusive lock, makes a copy, and publishes the mutated
  // object when scope ends. Copy-Mutate-Publish.
  Writer Edit() { return {*this}; }

 private:
  friend class Writer;
  mutable std::mutex mutex_;
  std::atomic<std::shared_ptr<const T>> ptr_;
};

}  // namespace hornet::data::utxo

#pragma once

#include <atomic>
#include <thread>

namespace hornet::data {

// A multiple-reader/single-writer mutex that prioritizes writers.
// Once a writer wants the exclusive lock, no new readers are granted access.
// Safe for re-entrancy for write-write locks, but not for any other access combination.
class PrioritySharedMutex {
 public:
  // Acquires a shared lock for read access.
  // Blocks if there is an active writer OR if writers are waiting to acquire the exclusive lock.
  void lock_shared() {
    while (true) {
      // 1. Wait if writers are waiting or active.
      if (writers_waiting_ > 0 || writer_active_.test()) {
        // Wait on writer_active_ first (exclusive holder).
        writer_active_.wait(true);

        // Also wait on writers_waiting_ if needed.
        // If writer_active_ is false, but writers_waiting_ > 0, we must wait.
        int ww = writers_waiting_;
        if (ww > 0) writers_waiting_.wait(ww);
        continue;
      }

      // 2. Optimistically increment reader count
      readers_active_++;

      // 3. Double check constraints after incrementing.
      if (writers_waiting_ > 0 || writer_active_.test()) {
        // Back off: we violated the writer preference or exclusivity.
        readers_active_--;
        readers_active_.notify_all();  // Wake up waiting writer.
        continue;
      }

      // Successfully acquired the read lock.
      break;
    }
  }

  // Releases a shared read lock.
  void unlock_shared() {
    if (--readers_active_ == 0)
      readers_active_.notify_all();  // If we were the last reader, notify waiting writers.
  }

  // Acquires an exclusive lock for write access.
  void lock() {
    // Check for re-entrancy on the same thread that already has the exclusive lock.
    const auto this_thread = std::this_thread::get_id();
    if (owner_thread_ == this_thread) {
      ++write_recursion_depth_;
      return;
    }

    // 1. Announce intent via writers_waiting_ (blocks new readers).
    ++writers_waiting_;
    writers_waiting_.notify_all();  // Wake up readers to force them to re-check and wait.

    // 2. Acquire exclusive access (wait for other writers to complete).
    while (writer_active_.test_and_set())
      writer_active_.wait(true);

    // 3. Wait for existing readers to complete, i.e. readers_active_ becomes zero.
    int r = readers_active_;
    while (r > 0) {
      readers_active_.wait(r);
      r = readers_active_;
    }

    // 4. Now we are active, decrement the waiting count.
    // Note: We stay "active" (writer_active_ is true), so readers are still blocked.
    writers_waiting_--;
    writers_waiting_.notify_all();  // Wake up readers waiting on the count.

    // 5. Record ownership for re-entrancy.
    owner_thread_ = this_thread;
    write_recursion_depth_ = 1;
  }

  // Releases an exclusive lock.
  void unlock() {
    if (--write_recursion_depth_ > 0) return;  // Handle recursion.
    owner_thread_ = std::thread::id{};  // Clear ownership.
    writer_active_.clear();
    writer_active_.notify_all();  // Wake up readers or other writers.
  }

 private:
  std::atomic<int> readers_active_ = 0;
  std::atomic<int> writers_waiting_ = 0;
  std::atomic_flag writer_active_;
  std::atomic<std::thread::id> owner_thread_;
  int write_recursion_depth_ = 0;
};

}  // namespace hornet::data

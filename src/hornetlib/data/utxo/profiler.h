#pragma once

#ifdef HORNET_PROFILING

#include <chrono>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "hornetlib/util/thread_safe_queue.h"
#include "hornetlib/util/timeout.h"

namespace hornet::data::utxo {

class Profiler {
 public:
  static void Log(std::string_view operation, std::chrono::microseconds duration, int height,
                  std::string_view extra = "") {
    static Profiler instance;
    instance.Submit(operation, duration, height, extra);
  }

 private:
  struct LogEntry {
    std::string operation;
    long long duration_us;
    int height;
    std::string extra;
  };

  Profiler() : file_("utxo_profile.csv", std::ios::trunc) {
    file_ << "Operation,Duration(us),Height,Extra\n";

    // Start background writer thread
    writer_thread_ = std::thread([this] {
      while (true) {
        // Wait for item, drain if stopped
        auto entry = queue_.WaitPop(util::Timeout::Infinite(), true);
        if (!entry) break;  // Stopped and empty
        Write(*entry);
      }
    });
  }

  ~Profiler() {
    queue_.Stop();
    if (writer_thread_.joinable()) writer_thread_.join();
  }

  void Submit(std::string_view operation, std::chrono::microseconds duration, int height,
              std::string_view extra) {
    queue_.Push({std::string(operation), duration.count(), height, std::string(extra)});
  }

  void Write(const LogEntry& entry) {
    file_ << entry.operation << "," << entry.duration_us << "," << entry.height << ","
          << entry.extra << "\n";
  }

  std::ofstream file_;
  util::ThreadSafeQueue<LogEntry> queue_;
  std::thread writer_thread_;
};

class ScopedProfiler {
 public:
  ScopedProfiler(std::string_view operation, int height, std::string_view extra = "")
      : operation_(operation),
        height_(height),
        extra_(extra),
        start_(std::chrono::steady_clock::now()) {}

  ~ScopedProfiler() {
    auto end = std::chrono::steady_clock::now();
    Profiler::Log(operation_, std::chrono::duration_cast<std::chrono::microseconds>(end - start_),
                  height_, extra_);
  }

 private:
  std::string operation_;
  int height_;
  std::string extra_;
  std::chrono::steady_clock::time_point start_;
};

}  // namespace hornet::data::utxo

#else  // HORNET_PROFILING

namespace hornet::data::utxo {

class ScopedProfiler {
 public:
  ScopedProfiler(std::string_view, int, std::string_view = "") {}
};

}  // namespace hornet::data::utxo

#endif  // HORNET_PROFILING

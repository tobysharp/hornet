#pragma once

#include <chrono>
#include <ctime>

namespace hornet::util {

struct Timing {
  std::chrono::nanoseconds thread_;  // Wall time spent in thread execution.
  std::chrono::nanoseconds cpu_;     // CPU time spent in thread execution.
};

struct TimePoint {
  std::chrono::high_resolution_clock::time_point start;
  timespec ts = {};

  TimePoint() {
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    start = std::chrono::high_resolution_clock::now();
  }
  TimePoint(const TimePoint&) = delete;

  Timing operator-(const TimePoint& rhs) const {
    return Timing{.thread_ = start - rhs.start,
                  .cpu_ = std::chrono::seconds{ts.tv_sec - rhs.ts.tv_sec} +
                          std::chrono::nanoseconds{ts.tv_nsec - rhs.ts.tv_nsec}};
  }
};

class Timer {
 public:
  class Scope {
   public:
    Scope(Timer& timer) : owner_(&timer) {}
    ~Scope() { End(); }
    void End() {
      if (owner_ == nullptr) return;
      owner_->AddTiming(TimePoint{} - start_);
      owner_ = nullptr;
    }
   private:
    Timer* owner_;
    TimePoint start_;
  };

  Scope AddScoped() { return *this; }

  auto Add(auto&& func) {
    const auto scoped = AddScoped();
    return func();
  }

  void AddTiming(const Timing& rhs) {
    thread_time_ += rhs.thread_.count();
    thread_cpu_ += rhs.cpu_.count();
    ++instances_;
  }

  Timer& operator +=(const Timer& rhs) {
    thread_time_ += rhs.thread_time_;
    thread_cpu_ += rhs.thread_cpu_;
    instances_ += rhs.instances_;
    return *this;
  }

  void Reset() { thread_time_ = thread_cpu_ = instances_ = 0; }

  double Seconds() const { return 1e-9 * thread_time_; }
  double CpuSeconds() const { return 1e-9 * thread_cpu_; }
  double Occupancy() const { return static_cast<double>(thread_cpu_) / thread_time_; }

  std::chrono::nanoseconds Nanoseconds() const { return std::chrono::nanoseconds{thread_time_}; }

  int64_t Instances() const { return instances_; }

 private:
  static std::chrono::duration<double> GetCpuTime() {
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0) {
      return std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec);
    }
    return {};
  }

  std::atomic<int64_t> thread_time_ = {};
  std::atomic<int64_t> thread_cpu_ = {};
  std::atomic<int64_t> instances_ = {};
};

}  // namespace hornet::util

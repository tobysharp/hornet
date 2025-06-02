// Logging facility for Hornet node infrastructure.
//
// Provides compile-time elision of log messages above a configured verbosity level,
// along with runtime configuration of output sinks and log level filtering.
//
// Usage is streaming-based and RAII-scoped for minimal overhead and clean syntax.
// Logging calls are fully disabled at compile time when above HORNET_MAX_LOG_LEVEL.
//
// Example usage:
//
// LogWarn() << "Are you sure about that? " << 42;
// LogInfo("That's cool but ", 6*9, " isn't.");
// LogError("Oh no") << " that was bad!";

#pragma once

#include <atomic>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>

// Compile with e.g. -DHORNET_MAX_LOG_LEVEL="None" to disable all logging at compile time.
#ifndef HORNET_MAX_LOG_LEVEL
#define HORNET_MAX_LOG_LEVEL Debug
#endif

namespace hornet::util {

// The logging levels in increasing verbosity.
enum class LogLevel { None, Error, Warn, Info, Debug };

// The LogContext singleton represents the target of logging output and is thread-safe.
class LogContext {
 public:
  static LogContext& Instance() {
    static LogContext instance;
    return instance;
  }

  void SetLevel(LogLevel level) {
    max_level_.store(level, std::memory_order_relaxed);
  }
  void SetOutputFile(const std::string& filename) {
    std::lock_guard lock(mutex_);
    file_.open(filename, std::ios::app);
  }
  void EnableStdout(bool enabled) {
    to_stdout_.store(enabled, std::memory_order_relaxed);
  }
  bool IsActive(LogLevel level) const {
    return static_cast<int>(level) <= static_cast<int>(max_level_.load(std::memory_order_relaxed));
  }

  void Emit(LogLevel level, const std::string& message) {
    std::lock_guard lock(mutex_);
    const std::string full = std::string(Prefix(level)) + message + "\n";
    if (to_stdout_) std::cout << full;
    if (file_.is_open()) file_ << full;
  }

 private:
  LogContext() = default;
  constexpr const char* Prefix(LogLevel level) const {
    switch (level) {
      case LogLevel::Debug:
        return "[DEBUG] ";
      case LogLevel::Info:
        return "[INFO ] ";
      case LogLevel::Warn:
        return "[WARN ] ";
      case LogLevel::Error:
        return "[ERROR] ";
      default:
        return "";
    }
  }

  mutable std::mutex mutex_;
  std::ofstream file_;
  std::atomic<LogLevel> max_level_ = LogLevel::HORNET_MAX_LOG_LEVEL;
  std::atomic<bool> to_stdout_ = true;
};

// A move-only RAII class that enables streaming to the LogContext with simple, clean syntax.
class LogLine {
 public:
  explicit LogLine(LogLevel level)
      : level_(level), active_(LogContext::Instance().IsActive(level)) {}
  LogLine(LogLine&& other) noexcept
      : level_(other.level_), active_(other.active_), buffer_(std::move(other.buffer_)) {
    other.active_ = false;
  }
  LogLine& operator=(LogLine&&) noexcept = default;

  template <typename T>
  LogLine& operator<<(const T& value) {
    if (active_) buffer_ << value;
    return *this;
  }

  ~LogLine() {
    if (active_) LogContext::Instance().Emit(level_, buffer_.str());
  }

 private:
  LogLine(const LogLine&) = delete;
  LogLine& operator=(const LogLine&) = delete;

  LogLevel level_;
  bool active_;
  std::ostringstream buffer_;
};

// A no-op null sink for logging call sites when logging is disabled.
class LogLineNull {
 public:
  template <typename T>
  constexpr LogLineNull& operator<<(const T&) noexcept {
    return *this;
  }
};

// The compile-time maximum log verbosity.
static constexpr LogLevel kMaxLogLevel = LogLevel::HORNET_MAX_LOG_LEVEL;

// Determines whether a logging level is compile-time enabled.
template <LogLevel kLevel>
inline constexpr bool IsLogLevelEnabled() {
  return static_cast<int>(kLevel) <= static_cast<int>(kMaxLogLevel);
}

// Makes the appropriate LogLine sink object for streaming syntax.
template <LogLevel kLevel>
inline auto MakeLogLine() {
  if constexpr (IsLogLevelEnabled<kLevel>()) {
    return LogLine{kLevel};
  } else {
    return LogLineNull{};
  }
}

// Variadic template form allows for passing logged objects as function arguments.
template <LogLevel kLevel, typename... Args>
inline auto Log(Args&&... args) {
  auto line = MakeLogLine<kLevel>();
  (void)(line << ... << std::forward<Args>(args));
  return line;
}

}  // namespace hornet::util

namespace hornet {

// Shortcuts for different logging verbosity levels in hornet namespace.

template <typename... Args>
inline auto LogError(Args&&... args) {
  return util::Log<util::LogLevel::Error>(std::forward<Args>(args)...);
}

template <typename... Args>
inline auto LogWarn(Args&&... args) {
  return util::Log<util::LogLevel::Warn>(std::forward<Args>(args)...);
}

template <typename... Args>
inline auto LogInfo(Args&&... args) {
  return util::Log<util::LogLevel::Info>(std::forward<Args>(args)...);
}

template <typename... Args>
inline auto LogDebug(Args&&... args) {
  return util::Log<util::LogLevel::Debug>(std::forward<Args>(args)...);
}

}  // namespace hornet

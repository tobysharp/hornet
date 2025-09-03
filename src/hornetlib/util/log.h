// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
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
#include <chrono>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

#include "hornetlib/util/notify.h"

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
  bool IsActive(LogLevel level) const {
    return static_cast<int>(level) <= static_cast<int>(max_level_.load(std::memory_order_relaxed));
  }
  void Emit(LogLevel level, const std::string& message) {
    auto LevelToString = [](LogLevel level) -> const char* {
      switch (level) {
        case LogLevel::None:
          return "";
        case LogLevel::Error:
          return "ERROR";
        case LogLevel::Warn:
          return "WARN";
        case LogLevel::Info:
          return "INFO";
        case LogLevel::Debug:
          return "DEBUG";
      }
    };
    using namespace std::chrono;
    NotifyLog(
          {{"time_us", duration_cast<microseconds>(system_clock::now().time_since_epoch()).count()},
            {"level", LevelToString(level)},
            {"msg", std::string{message}}});
    }

 private:
  LogContext() = default;
  std::string Prefix(LogLevel level) const {
    const std::string prefix = [&]() {
      switch (level) {
        case LogLevel::Debug:
          return "[DEBUG";
        case LogLevel::Info:
          return "[INFO ";
        case LogLevel::Warn:
          return "[WARN ";
        case LogLevel::Error:
          return "[ERROR";
        default:
          return "";
      }
    }();
    return prefix + Time() + "] ";
  }

  static std::string Time() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    std::string time = std::format("{:%H:%M:%S}", floor<seconds>(now));
    const auto us = duration_cast<microseconds>(now.time_since_epoch()) % 1'000'000;
    return time + std::format(".{:04}", us.count() / 100);
  }

  std::atomic<LogLevel> max_level_ = LogLevel::HORNET_MAX_LOG_LEVEL;
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

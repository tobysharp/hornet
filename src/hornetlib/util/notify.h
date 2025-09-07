// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

namespace hornet::util {

using NotificationValue = std::variant<int64_t, double, std::string>;

class NotificationMap {
 public:
  using Entry = std::pair<std::string_view, NotificationValue>;

  NotificationMap() = default;
  NotificationMap(std::initializer_list<Entry> list) : map_(list) {}
  NotificationMap(const NotificationMap&) = default;
  NotificationMap(NotificationMap&&) = default;
  NotificationMap& operator =(const NotificationMap&) = default;
  NotificationMap& operator =(NotificationMap&&) = default;

  bool Empty() const {
    return map_.empty();
  }
  int Size() const {
    return std::ssize(map_);
  }
  void Insert(std::string_view key, NotificationValue value) {
    map_.emplace_back(std::pair{std::move(key), std::move(value)});
  }
  template <typename T>
  const T* Find(std::string_view key) const {
    for (const auto& [k, v] : map_)
        if (k == key) return &std::get<T>(v);
    return nullptr;
  }

  auto begin() const { return map_.begin(); }
  auto end() const { return map_.end(); }

 private:
  std::vector<Entry> map_;
};

void NotifyEvent(std::string path, NotificationMap values);
void NotifyMetric(std::string path, NotificationMap values);
void NotifyLog(NotificationMap values);

enum class NotificationType {
    Log,        // Console messages.
    Discrete,   // One-time events.
    Continuous  // Updating metrics.
};

struct NotificationPayload {
    NotificationType type;
    std::string path;
    NotificationMap map;
};

using NotificationSink = std::function<void(NotificationPayload)>;

// The notification sink is called once per Notify call. Must be thread-safe and non-blocking.
void SetNotificationSink(NotificationSink sink);

// The default notification sink class that writes logs only to stdout and/or appends to file.
class DefaultLogSink {
 public:
  static DefaultLogSink& Instance();
  void SetOutputFile(const std::string& filename);
  void EnableStdout(bool enabled);
  void operator()(NotificationPayload payload);

  static void Log(NotificationPayload payload) {
    Instance()(payload);
  }

 private:
  DefaultLogSink() = default;
  std::string Prefix(const std::string& level, int64_t time_us) const;

  mutable std::mutex mutex_;
  std::ofstream file_;
  bool to_stdout_ = true;
};

}  // namespace hornet::util

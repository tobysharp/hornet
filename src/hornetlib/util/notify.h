#pragma once

#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace hornet::util {

using NotificationValue = std::variant<int64_t, double, std::string>;
using NotificationMap = std::unordered_map<std::string_view, NotificationValue>;

void NotifyEvent(std::string_view path, const NotificationMap& values);
void NotifyMetric(std::string_view path, const NotificationMap& values);
void NotifyLog(const NotificationMap& values);

enum class NotificationType {
    Log,        // Console messages.
    Discrete,   // One-time events.
    Continuous  // Updating metrics.
};

struct NotificationPayload {
    NotificationType type;
    std::string_view path;
    const NotificationMap& map;
};

using NotificationSink = std::function<void(const NotificationPayload&)>;

// The notification sink is called once per Notify call. Must be thread-safe and non-blocking.
void SetNotificationSink(NotificationSink sink);

// The default notification sink class that writes logs only to stdout and/or appends to file.
class DefaultLogSink {
 public:
  static DefaultLogSink& Instance();
  void SetOutputFile(const std::string& filename);
  void EnableStdout(bool enabled);
  void operator()(const NotificationPayload& payload);

 private:
  DefaultLogSink() = default;
  std::string Prefix(const std::string& level, int64_t time_us) const;

  mutable std::mutex mutex_;
  std::ofstream file_;
  bool to_stdout_ = true;
};

}  // namespace hornet::util

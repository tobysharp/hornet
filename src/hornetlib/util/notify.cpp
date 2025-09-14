#include "hornetlib/util/notify.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "hornetlib/util/log.h"

namespace hornet::util {

DefaultLogSink& DefaultLogSink::Instance() {
  static DefaultLogSink instance;
  return instance;
}

void DefaultLogSink::SetOutputFile(const std::string& filename) {
  std::lock_guard lock(mutex_);
  file_.open(filename, std::ios::app);
}

void DefaultLogSink::EnableStdout(bool enabled) {
  std::lock_guard lock(mutex_);
  to_stdout_ = enabled;
}

void DefaultLogSink::operator()(NotificationPayload payload) {
  if (payload.type != NotificationType::Log)
    return;

  std::lock_guard lock(mutex_);
  const LogLevel level = LogLevel(*payload.map.Find<int64_t>("level"));
  const std::string& message = *payload.map.Find<std::string>("msg");
  const int64_t time_us = *payload.map.Find<int64_t>("time_us");
  const std::string full = FormatLogLine(level, time_us, message) + "\n";

  if (to_stdout_) std::cout << full;
  if (file_.is_open()) file_ << full;
}

namespace {
NotificationSink notification_sink = &DefaultLogSink::Log;
}  // namespace

void Notify(NotificationType type, std::string path, NotificationMap values) {
  notification_sink({type, std::move(path), std::move(values)});
}

void NotifyLog(NotificationMap values) {
  Notify(NotificationType::Log, {}, std::move(values));
}

void NotifyEvent(std::string path, std::string message, EventType type) {
  Notify(NotificationType::Discrete, std::move(path), {{"kind", int(type)}, {"msg",std::move(message)}});
}

void NotifyMetric(std::string path, NotificationMap values) {
  Notify(NotificationType::Continuous, std::move(path), std::move(values));
}

void SetNotificationSink(NotificationSink sink) {
  notification_sink = std::move(sink);
}

}  // namespace hornet::util

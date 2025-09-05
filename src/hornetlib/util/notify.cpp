#include "hornetlib/util/notify.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <unordered_map>

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
  const std::string& level = std::get<std::string>(*payload.map.Find("level"));
  const std::string& message = std::get<std::string>(*payload.map.Find("msg"));
  const int64_t time_us = std::get<int64_t>(*payload.map.Find("time_us"));
  const std::string full = Prefix(level, time_us) + message + "\n";

  if (to_stdout_) std::cout << full;
  if (file_.is_open()) file_ << full;
}

std::string DefaultLogSink::Prefix(const std::string& level, int64_t time_us) const {
  using namespace std::chrono;
  const sys_time<microseconds> tp{microseconds{time_us}};
  return "[" + std::string{level} + (level.size() < 5 ? " " : "") +
         std::format(" {:%H:%M:%S}.{:04}", floor<seconds>(tp),
                     (tp.time_since_epoch() % 1s).count() / 100) +
         "] ";
}

namespace {
NotificationSink notification_sink = [](NotificationPayload payload) {
  DefaultLogSink::Instance()(std::move(payload));
};
}  // namespace

void Notify(NotificationType type, std::string path, NotificationMap values) {
  notification_sink({type, std::move(path), std::move(values)});
}

void NotifyLog(NotificationMap values) {
  Notify(NotificationType::Log, {}, std::move(values));
}

void NotifyEvent(std::string path, NotificationMap values) {
  Notify(NotificationType::Discrete, std::move(path), std::move(values));
}

void NotifyMetric(std::string path, NotificationMap values) {
  Notify(NotificationType::Continuous, std::move(path), std::move(values));
}

void SetNotificationSink(NotificationSink sink) {
  notification_sink = std::move(sink);
}

}  // namespace hornet::util

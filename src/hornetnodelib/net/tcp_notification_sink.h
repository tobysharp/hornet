#pragma once

#include <atomic>
#include <iostream>
#include <memory>
#include <thread>

#include "hornetlib/util/as_span.h"
#include "hornetlib/util/notify.h"
#include "hornetlib/util/thread_safe_queue.h"
#include "hornetnodelib/net/connection.h"

namespace hornet::node::net {

// A notification sink which sends notifications as JSON strings over a TCP connection.
class TcpNotificationSink {
 public:
  TcpNotificationSink(const std::string& host, uint16_t port, bool blocking = false)
      : connection_(host, port, blocking), worker_{&TcpNotificationSink::RunWorker, this} {}

  ~TcpNotificationSink() {
    abort_ = true;
    if (worker_.joinable()) worker_.join();
    if (dropped_ > 0)
      std::cerr << "TcpNotificationSink dropped " << dropped_ << " total frames." << std::endl;
  }

  void operator()(util::NotificationPayload item) {
    static constexpr int kMaxQueueSize = 1 << 12;  // 4,096

    // Duplicate console warnings and errors to stderr.
    if (item.type == util::NotificationType::Log) {
      const auto* level = item.map.Find<std::string>("level");
      if (level != nullptr && (*level == "WARN" || *level == "ERROR")) {
        if (const auto* msg = item.map.Find<std::string>("msg"))
          std::cerr << *msg << std::endl;
      }
    }

    // Try push to payload queue. Follow policy if full (probably drop oldest).
    if (!abort_) {
      if (queue_.Size() >= kMaxQueueSize) {
        // Note this isn't atomic with Push, but that doesn't particularly matter here.
        queue_.TryPop();  // Silently drop oldest from queue
        dropped_++;
      }
      queue_.Push(std::move(item));
    }
  }

 private:
  void RunWorker() {
    // This timeout represents the maximum time (in milliseconds) that we could be blocked while
    // the queue is filling up and we're not servicing it.
    static constexpr int kMaxPollTimeoutMs = 25;

    std::string output;
    output.reserve(1 << 14);  // 16 KB
    while (!abort_ || !queue_.Empty()) {
      // Format any items on our queue ready for streaming
      for (output.clear(); auto item = queue_.TryPop();)
        output = FormatJson(*item, std::move(output));
      if (!output.empty()) {
        const auto ptr = std::make_shared<std::string>(std::move(output));
        const auto span = util::AsByteSpan(std::span{*ptr});
        connection_.EnqueueWrite({span, ptr});
      }

      // Now a possibly blocking poll and write to the socket.
      if (!connection_.PollToWrite(kMaxPollTimeoutMs, true)) {
        abort_ = true;  // Fatal error. This sink will never work again.
        // TODO: Optionally throw a specific exception so the app can create a new sink.
        // https://linear.app/hornet-node/issue/HOR-55/handle-connection-drop-in-tcpnotificationsink-worker-thread
      }
    }
  }

  std::string FormatJson(const util::NotificationPayload& item, std::string s) {
    s += R"({"type":")";
    s += ToString(item.type);
    s += R"(","path":")";
    s += item.path;
    s += '"';
    for (const auto& [key, value] : item.map) {
      s += ",\"";
      s += key;
      s += "\":";
      std::visit(
          [&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::string>) {
              s += '"';
              s += val;
              s += '"';
            } else {
              s += std::to_string(val);
            }
          },
          value);
    }
    s += "}\n";
    return s;
  }

  static std::string_view ToString(util::NotificationType type) {
    switch (type) {
      case util::NotificationType::Log:
        return "log";
      case util::NotificationType::Discrete:
        return "event";
      case util::NotificationType::Continuous:
        return "update";
    }
  }

  Connection connection_;
  std::thread worker_;
  util::ThreadSafeQueue<util::NotificationPayload> queue_;
  std::atomic<bool> abort_ = false;
  int dropped_ = 0;
};

}  // namespace hornet::node::net

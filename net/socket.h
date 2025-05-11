#pragma once

#include <cstdint>
#include <iostream>
#include <poll.h>
#include <span>
#include <string>

namespace hornet::net {

class Socket {
 public:
  static Socket Connect(const std::string& host, uint16_t port);

  Socket(int fd);
  ~Socket();

  Socket(Socket&& other) noexcept;
  Socket& operator=(Socket&& other) noexcept;

  void Close() {
    if (fd_ >= 0) {
      close(fd_);
      fd_ = -1;
    }
  }

  bool IsOpen() const {
    return fd_ >= 0;
  }

  void Write(std::span<const uint8_t> data) const;

  // Reads data from the socket, blocking if data is not currently
  // available to be read. Use HasReadData to check for available data.
  size_t Read(std::span<uint8_t> buffer) const;

  int GetFD() const {
    return fd_;
  }

  // Checks whether data is ready to be read.
  // timeout_ms ==  0: return immediately (non-blocking)
  // timeout_ms == -1: block indefinitely
  // timeout_ms  >  0: wait for specified time (in milliseconds)
  bool HasReadData(int timeout_ms = 0) const {
    if (fd_ < 0) {
        throw std::runtime_error("HasReadData on closed socket.");
    }
    pollfd pfd = {fd_, POLLIN, 0};
    int result = poll(&pfd, 1, timeout_ms);
    return (result > 0) && (pfd.revents & POLLIN);
  }

 private:
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  int fd_;
};

}  // namespace hornet::net

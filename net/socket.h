#pragma once

#include <cstdint>
#include <iostream>
#include <optional>
#include <poll.h>
#include <span>
#include <string>

namespace hornet::net {

class Socket {
 public:
  static Socket Connect(const std::string& host, uint16_t port, bool blocking = true);

  Socket() : fd_(-1) {}
  Socket(int fd, bool blocking = true);
  ~Socket();

  Socket(Socket&& other) noexcept;
  Socket& operator=(Socket&& other) noexcept;

  void Close();

  bool IsOpen() const {
    return fd_ >= 0;
  }
  bool IsBlocking() const {
    return is_blocking_;
  }
  
  std::optional<size_t> Write(std::span<const uint8_t> data) const;

  // Reads data from the socket, blocking if data is not currently
  // available to be read. Use HasReadData to check for available data.
  std::optional<size_t> Read(std::span<uint8_t> buffer) const;

  int GetFD() const {
    return fd_;
  }

  // Checks whether data is ready to be read.
  // timeout_ms ==  0: return immediately (non-blocking)
  // timeout_ms == -1: block indefinitely
  // timeout_ms  >  0: wait for specified time (in milliseconds)
  bool HasReadData(int timeout_ms = 0) const;

  // Returns the number of bytes available to read at this moment.
  // Non-blocking.
  size_t GetReadCapacity() const;

 private:
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  int fd_;
  bool is_blocking_ = true;
};

}  // namespace hornet::net

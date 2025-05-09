#pragma once

#include <cstdint>
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

  void Write(std::span<const uint8_t> data) const;
  size_t Read(std::span<uint8_t> buffer) const;

  int GetFD() const {
    return fd_;
  }

 private:
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  int fd_;
};

}  // namespace hornet::net

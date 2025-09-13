// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include <cerrno>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "hornetlib/util/log.h"
#include "hornetlib/util/throw.h"
#include "hornetnodelib/net/socket.h"

namespace hornet::node::net {

namespace {
inline ssize_t Send(int fd, const void* buf, size_t len) {
#if defined(__linux__)
  return ::send(fd, buf, len, MSG_NOSIGNAL);
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
  return ::send(fd, buf, len, 0);  // SO_NOSIGPIPE should be set once at socket creation.
#else
  return ::write(fd, buf, len);    // fallback; caller may need SIGPIPE ignored globally.
#endif
}
}  // namespace

Socket::Socket(int fd, bool blocking /* = true */) : fd_(fd), is_blocking_(blocking) {
  if (fd_ < 0) {
    throw std::runtime_error("Invalid socket descriptor");
  }
}

Socket::~Socket() {
  Close();
}

Socket::Socket(Socket &&other) noexcept : fd_(other.fd_), is_blocking_(other.is_blocking_) {
  other.fd_ = -1;
  other.is_blocking_ = true;
}

Socket &Socket::operator=(Socket &&other) noexcept {
  if (this != &other) {
    Close();
    std::swap(fd_, other.fd_);
    std::swap(is_blocking_, other.is_blocking_);
  }
  return *this;
}

void Socket::Close() {
  if (fd_ >= 0) {
    ::tcp_info ti{};
    socklen_t length = sizeof(ti);
    ::getsockopt(fd_, IPPROTO_TCP, TCP_INFO, &ti, &length);
    LogWarn() << "Closing socket with fd=" << fd_ << ", errno=" << errno << ", tcp_info.state=" << int(ti.tcpi_state);

    close(fd_);
    fd_ = -1;
  }
}

/* static */ Socket Socket::Connect(const std::string &host, uint16_t port, bool blocking /* = true */) {
  addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo *res = nullptr;
  int err = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
  if (err != 0 || !res) {
    throw std::runtime_error("Failed to resolve address: " + std::string(gai_strerror(err)));
  }

  int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) {
    freeaddrinfo(res);
    throw std::runtime_error("Failed to create socket");
  }

  if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
    close(fd);
    freeaddrinfo(res);
    throw std::runtime_error("Failed to connect: " + std::string(std::strerror(errno)));
  }
  freeaddrinfo(res);
  res = nullptr;

  if (!blocking) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
      close(fd);
      throw std::runtime_error("Failed to get socket flags");
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
      close(fd);
      throw std::runtime_error("Failed to set non-blocking mode"); 
    }
  }

  LogInfo() << "Socket opened with fd=" << fd << ".";
  return Socket(fd, blocking);
}

// Checks whether data is ready to be read.
// timeout_ms ==  0: return immediately (non-blocking)
// timeout_ms == -1: block indefinitely
// timeout_ms  >  0: wait for specified time (in milliseconds)
bool Socket::HasReadData(int timeout_ms /* = 0 */) const {
  if (fd_ < 0) {
      throw std::runtime_error("HasReadData on closed socket.");
  }
  pollfd pfd = {fd_, POLLIN, 0};
  int result = poll(&pfd, 1, timeout_ms);
  return (result > 0) && (pfd.revents & POLLIN);
}

// Returns the number of bytes available to read at this moment.
// Non-blocking.
int32_t Socket::GetReadCapacity() const {
  int bytes_available = 0;
  if (ioctl(fd_, FIONREAD, &bytes_available) == 0) {
    return bytes_available;
  }
  return 0;
}
  
std::optional<int> Socket::Write(std::span<const uint8_t> data) const {
  constexpr int kMaxRetries = 5;
  if (fd_ < 0)
    util::ThrowRuntimeError("Write on closed socket, fd=", fd_, ".");

  for (int i = 0; i < kMaxRetries; ++i) {
    const ssize_t n = Send(fd_, data.data(), data.size());
    if (n >= 0) return n;
    const int error = errno;
    if (error == EINTR) continue;  // Retry immediately.
    if (error == EAGAIN || error == EWOULDBLOCK) return {};  // Non-blocking mode with full pipe.
    util::ThrowRuntimeError("Socket write failed: ", std::strerror(error), " (errno ", error, ")");
  }
  return {};  // All retries failed, try again later.
}

std::optional<int> Socket::Read(std::span<uint8_t> buffer) const {
  static constexpr int kMaxRetries = 10;
  if (fd_ < 0) {
    throw std::runtime_error("Read on closed socket.");
  }
  int e = 0;
  for (int i = 0; i < kMaxRetries; ++i) {
    ssize_t n = read(fd_, buffer.data(), buffer.size());
    if (n > 0) return n;
    if (n == 0) {
      LogWarn() << "Socket read fd=" << fd_ << " returned with EOF.";
      return {};  // EOF, drop connection cleanly
    }
    e = errno;
    if (e == EINTR) continue;  // Retry
    if (e == EAGAIN || e == EWOULDBLOCK) return 0;  // No data, try later
    if (e == ECONNRESET || e == ETIMEDOUT || e == ENOTCONN || e == EIO) {
      LogWarn() << "Socket read fd=" << fd_ << " returned with errno=" << e << ": \"" << std::strerror(e) << "\".";
      return {};  // Treat as EOF.
    }
  }
  util::ThrowRuntimeError("Socket read failed: ", std::strerror(e), " (errno ", e, ")");
}

}  // namespace hornet::node::net

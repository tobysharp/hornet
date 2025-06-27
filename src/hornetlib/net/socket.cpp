// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "hornetlib/net/socket.h"

namespace hornet::net {

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
size_t Socket::GetReadCapacity() const {
  int bytes_available = 0;
  if (ioctl(fd_, FIONREAD, &bytes_available) == 0) {
    return static_cast<size_t>(bytes_available);
  }
  return 0;
}
  
std::optional<size_t> Socket::Write(std::span<const uint8_t> data) const {
  if (fd_ < 0) {
    throw std::runtime_error("Write on closed socket.");
  }
  ssize_t n = write(fd_, data.data(), data.size());
  if (n < 0) {
    const int error = errno;
    if ((error == EAGAIN) || (error == EWOULDBLOCK))
      return {};  // Non-blocking mode with full pipe.
  throw std::runtime_error("Socket write failed");
  }
  return static_cast<size_t>(n);
}

std::optional<size_t> Socket::Read(std::span<uint8_t> buffer) const {
  if (fd_ < 0) {
    throw std::runtime_error("Read on closed socket.");
  }
  ssize_t n = read(fd_, buffer.data(), buffer.size());
  if (n < 0) {
    const int error = errno;
    if ((error == EAGAIN) || (error == EWOULDBLOCK))
      return {};  // Non-blocking mode without data.
    throw std::runtime_error("Socket read failed");
  }
  return static_cast<size_t>(n);
}

}  // namespace hornet::net

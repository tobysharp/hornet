#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include "net/socket.h"

namespace hornet::net {

Socket::Socket(int fd) : fd_(fd) {
  if (fd_ < 0) {
    throw std::runtime_error("Invalid socket descriptor");
  }
}

Socket::~Socket() {
  if (fd_ >= 0) {
    close(fd_);
  }
}

Socket::Socket(Socket &&other) noexcept : fd_(other.fd_) {
  other.fd_ = -1;
}

Socket &Socket::operator=(Socket &&other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) {
      close(fd_);
    }
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}

/* static */ Socket Socket::Connect(const std::string &host, uint16_t port) {
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
  return Socket(fd);
}

void Socket::Write(std::span<const uint8_t> data) const {
  const uint8_t *ptr = data.data();
  size_t remaining = data.size();
  while (remaining > 0) {
    ssize_t n = write(fd_, ptr, remaining);
    if (n <= 0) throw std::runtime_error("Socket write failed");
    ptr += n;
    remaining -= n;
  }
}

size_t Socket::Read(std::span<uint8_t> buffer) const {
  ssize_t n = read(fd_, buffer.data(), buffer.size());
  if (n < 0) throw std::runtime_error("Socket read failed");
  return static_cast<size_t>(n);
}

}  // namespace hornet::net

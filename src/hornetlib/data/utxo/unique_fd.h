#pragma once

#include <unistd.h>

namespace hornet::data::utxo {

class UniqueFD {
 public:
  UniqueFD() noexcept : fd_(-1) {}
  explicit UniqueFD(int fd) noexcept : fd_(fd) {}
  UniqueFD(const UniqueFD&) = delete;
  UniqueFD(UniqueFD&& rhs) noexcept : fd_(rhs.fd_) {
    rhs.fd_ = -1;
  }
  ~UniqueFD() {
    if (fd_ >= 0) ::close(fd_);
  }

  UniqueFD& operator =(const UniqueFD&) = delete;
  UniqueFD& operator =(UniqueFD&& rhs) {
    if (this != &rhs) {
      Reset(rhs.fd_);
      rhs.fd_ = -1;
    }
    return *this;
  }

  explicit operator bool() const noexcept { return fd_ >= 0; }
  operator int() const noexcept { return fd_; }

  int Release() {
    int rv = fd_;
    fd_ = -1;
    return rv;
  }

  void Reset(int fd = -1) {
    if (fd_ >= 0) ::close(fd_);
    fd_ = fd;
  }

 private:
  int fd_;
};

}  // namespace hornet::data::utxo

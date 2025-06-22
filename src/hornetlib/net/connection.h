// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "net/socket.h"
#include "util/shared_span.h"

#include <deque>
#include <vector>

namespace hornet::net {

class Connection {
 public:
  Connection(const std::string& host, uint16_t port)
      : sock_(Socket::Connect(host, port)) {}

  // Writes at least some of the buffer directly to the socket.
  // In order to guarantee non-blocking behavior, ensure this method is
  // called after poll() sgnals POLLOUT. 
  // The amount of data written may be less than the total buffer, but 
  // should be more than zero unless the socket is being closed.
  size_t Write(std::span<const uint8_t> buffer) {
    if (buffer.empty() || !sock_.IsOpen()) return 0;

    const auto write_bytes = sock_.Write(buffer);
    if (!write_bytes) {
      // Non-blocking mode without available data. Very surprising, but not
      // an error. Worth logging since data was signaled via POLLIN and FIONREAD.
      return 0;
    }
    else if (*write_bytes == 0) {
      // Successful write of zero bytes -- close the socket.
      sock_.Close();
    }
    return *write_bytes;
  }

  const Socket& GetSocket()const {
    return sock_;
  }

  Socket& GetSocket() {
    return sock_;
  }
  
  // Reads up to n bytes from the socket into this class's internal buffer, 
  // growing it as necessary. In order to guarantee non-blocking behavior,
  // ensure this method is called after poll() signals POLLIN.
  size_t ReadToBuffer(size_t n) {
     if (!sock_.IsOpen()) return 0;
    
    // Detect how many bytes are available to read. Fast, non-blocking.
    const size_t bytes_available = sock_.GetReadCapacity();
    // This shouldn't return zero if the poll() returned POLLIN,
    // but we allow it to continue in case data arrives now, or the
    // socket is in blocking mode.
    if (bytes_available > 0) {
      n = std::min(n, bytes_available);
    }
 
    // In the case of multi-threading, take a lock on a mutex here, since
    // after this point the size of receive_buffer_ doesn't match the amount
    // of valid data in it.
    const size_t initial_size = buffer_.size();
    // Here we reserve the maximum known future size of the buffer to prevent
    // further reallocations and memory moves if multiple chunks are required.
    // OPT: As a later optimization, we can request the buffer here from a 
    // size-classed, LRU-expunged, size-capped custom allocator.
    buffer_.insert(buffer_.end(), std::max(n, bytes_available), 0);
    const auto read_bytes = sock_.Read({buffer_.data() + initial_size, n});
    if (!read_bytes) {
      // Non-blocking mode without available data. Very surprising, but not
      // an error. Worth logging since data was signaled via POLLIN and FIONREAD.
      buffer_.resize(initial_size);
      return 0;
    }
    else if (*read_bytes == 0) {
      buffer_.resize(initial_size);
      sock_.Close();
      return 0;
    }
    buffer_.resize(initial_size + *read_bytes);
    // The mutex can be released here.

    return *read_bytes;
  }

  std::span<const uint8_t> PeekBufferedData() const {
    return { buffer_.begin() + read_cursor_, buffer_.end() };
  }

  void ConsumeBufferedData(size_t bytes) {
    read_cursor_ = std::min(read_cursor_ + bytes, buffer_.size());
  }

  void EnqueueWrite(util::SharedSpan<const uint8_t> buffer) {
    if (!buffer || buffer->empty()) return;
    write_queue_.emplace_back(std::move(buffer));
  }

  size_t QueuedWriteBufferCount() const {
    return write_queue_.size();
  }

  size_t ContinueWrite() {
    if (!sock_.IsOpen()) return 0;
    const bool is_blocking = sock_.IsBlocking();
    size_t bytes_written = 0;
    do {
      while (!write_queue_.empty() && write_queue_.front()->empty())
        write_queue_.pop_front();
      if (!write_queue_.empty()) {
        auto& span = write_queue_.front();
        const auto write = sock_.Write(*span);
        if (!write) {
          // Non-blocking socket not ready for writing. It's not an error.
          break;
        }
        else if (*write == 0) {
          // Failed to write. Must drop the connection now.
          Drop();
          return 0;
        }
        // Move the write cursor
        span.Skip(*write);
        bytes_written += *write;
        if (span->empty()) write_queue_.pop_front();
      }
    } while (!is_blocking && !write_queue_.empty());
    return bytes_written;
  }

  // Drops the connection by closing the socket and clearing the read buffer.
  void Drop() {
    buffer_.clear();
    read_cursor_ = 0;
    write_queue_.clear();
    sock_.Close();
  }

  void TrimBufferedData() {
    if (read_cursor_ == 0) return;

    if (read_cursor_ == buffer_.size()) {
        buffer_.clear();
        read_cursor_ = 0;
    } else {
        buffer_.erase(buffer_.begin(), buffer_.begin() + read_cursor_);
        read_cursor_ = 0;
    }
  }
 private:

  Socket sock_;
  std::vector<uint8_t> buffer_;
  size_t read_cursor_ = 0;
  std::deque<util::SharedSpan<const uint8_t>> write_queue_;
};

}  // namespace hornet::net

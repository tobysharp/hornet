#pragma once

#include <cstdint>
#include <span>

namespace hornet::data::utxo {

struct IORequest {
  int fd;
  uint64_t offset;
  int length;
  uint8_t* buffer;
  uintptr_t user;
};

/*
class IOEngine {
 public:
  void Submit(std::span<const IORequest> requests);
  size_t Reap(std::span<IORequest*> results);
  IORequest* WaitOne();
};
*/

template <typename Engine>
concept IOEngine = requires(Engine engine) {
  { engine.Submit(std::span<const IORequest>{}) } -> std::same_as<int>;
  { engine.Reap(std::span<const IORequest*>{}) } -> std::same_as<int>;
  { engine.WaitOne() } -> std::same_as<const IORequest*>;
  { Engine::GetQueueDepth() } -> std::same_as<int>;
};

template <typename Engine>
requires IOEngine<Engine>
void Read(Engine& io, std::span<const IORequest> requests) {
  int submitted = 0;
  int completed = 0;
  std::array<const IORequest*, Engine::GetQueueDepth()> results;
  while (completed < std::ssize(requests)) {
    if (submitted < std::ssize(requests))
      submitted += io.Submit(std::span{requests}.subspan(submitted));
    int ready = io.Reap(results);
    if (ready == 0) {
      results[0] = io.WaitOne();
      ready = 1;
    }
  }
}

}  // namespace hornet::data::utxo

// Linux back-end.
// TODO: Move into platform-specific area later.

#include <liburing.h>

#include "hornetlib/util/throw.h"

namespace hornet::data::utxo {

class UringIOEngine {
 public:
  UringIOEngine() {
    if (::io_uring_queue_init(kQueueDepth, &ring_, 0) < 0)
      util::ThrowRuntimeError("io_uring_queue_init failed.");
  }
  ~UringIOEngine() {
    ::io_uring_queue_exit(&ring_);
  }

  static constexpr int GetQueueDepth() { return kQueueDepth; }

  int Submit(std::span<const IORequest> requests) {
    int queued = 0;
    for (const auto& request : requests) {
      if (!::io_uring_sq_space_left(&ring_)) break;
      io_uring_sqe* sqe = ::io_uring_get_sqe(&ring_);
      ::io_uring_prep_read(sqe, request.fd, request.buffer, request.length, request.offset);
      ::io_uring_sqe_set_data(sqe, const_cast<IORequest*>(&request));
      ++queued;
    }
    if (queued > 0) ::io_uring_submit(&ring_);
    return queued;
  }

  int Reap(std::span<const IORequest*> results) {
    std::array<io_uring_cqe*, kQueueDepth> cqes;
    const int size = std::min<int>(std::ssize(results), kQueueDepth);
    size_t count = ::io_uring_peek_batch_cqe(&ring_, &cqes[0], size);
    for (size_t i = 0; i < count; ++i) {
      results[i] = static_cast<const IORequest*>(::io_uring_cqe_get_data(cqes[i]));
      ::io_uring_cqe_seen(&ring_, cqes[i]);
    }
    return count;
  }

  const IORequest* WaitOne() {
    io_uring_cqe* cqe;
    ::io_uring_wait_cqe(&ring_, &cqe);
    const IORequest* rv = static_cast<const IORequest*>(::io_uring_cqe_get_data(cqe));
    ::io_uring_cqe_seen(&ring_, cqe);
    return rv;
  }

 private:
  static constexpr int kQueueDepth = 64;
  io_uring ring_;
};

}  // namespace hornet::data::utxo

#include <chrono>
#include <cstring>
#include <poll.h>
#include <stdexcept>

#include "encoding/reader.h"
#include "net/connection.h"
#include "protocol/dispatch.h"
#include "protocol/message.h"
#include "protocol/parser.h"

namespace hornet::net {

std::unique_ptr<protocol::Message> Connection::NextMessage(int timeout_ms /* = 0 */) {
  using namespace std::chrono_literals;

  // Start by computing the time now and the time we should exit this function.
  const auto time_at_entry = std::chrono::high_resolution_clock::now();
  const auto time_to_exit = time_at_entry + std::chrono::milliseconds{timeout_ms};

  size_t read_bytes = 0;
  do {
    // Calculate how much time is remaining on the clock
    const auto time_remaining_ms =
        std::max(0ms, std::chrono::duration_cast<std::chrono::milliseconds>(
                          time_to_exit - std::chrono::high_resolution_clock::now()));

    // If there is no time remaining on our clock, we pass 0ms here for non-blocking
    if (!sock_.HasReadData(static_cast<int>(time_remaining_ms.count()))) {
      // Return nullptr if we didn't get enough data to parse into a message.
      return nullptr;
    }

    // Make sure the buffer is big enough to write at least 2KiB more data.
    constexpr size_t kChunkSize = 2048;
    if (buffer_.size() < write_at_ + kChunkSize) {
      buffer_.resize(write_at_ + kChunkSize);
    }

    // Read directly into our buffer and update the write cursor.
    // We now know there is data available so this will be non-blocking.
    const size_t length = sock_.Read({buffer_.begin() + write_at_, buffer_.end()});
    write_at_ += length;
    read_bytes += length;

    // Throw on read_bytes exceeding some limit to defend against misbehavior.
    if (read_bytes > kReadLimit) {
      throw std::runtime_error("Exceeded per-message read limit.");
    }

    // If the data buffer now contains a whole message we should parse it.
    // cursor_ is an offset to the start of the unparsed buffered data.
    if (IsCompleteMessageBuffered()) {
      return TryParseMessage();
    }
  } while (true);
}

std::span<const uint8_t> Connection::GetUnparsedData() const {
    return {buffer_.begin() + parse_at_,
        write_at_ - parse_at_};
}

bool Connection::IsCompleteMessageBuffered() const {
  // If we don't have a full 24-byte header, we don't have a full message.
  if (parse_at_ + protocol::kHeaderLength >= write_at_) return false;

  // Use the parser to determine if we have the full payload buffered.
  return parser_.IsCompleteMessage(GetUnparsedData());
}

std::unique_ptr<protocol::Message> Connection::TryParseMessage() {
  const auto parsed = parser_.Parse(GetUnparsedData());
  auto message = factory_.Create(parsed.header.command);
  encoding::Reader reader{parsed.payload};
  message->Deserialize(reader);
  parse_at_ += protocol::kHeaderLength + parsed.payload.size();
  if (parse_at_ == write_at_) {
    // We have parsed all the data in our buffer, so now we can reuse it.
    buffer_.clear();
    parse_at_ = write_at_ = 0;
  } else if (parse_at_ > kTrimThreshold) {
    buffer_.erase(buffer_.begin(), buffer_.begin() + parse_at_);
    write_at_ -= parse_at_;
    parse_at_ = 0;
  }
  return message;
}

bool Connection::IsPartial() const {
  return !buffer_.empty();
}

bool Connection::IsWaiting() const {
  pollfd pfd = {sock_.GetFD(), POLLIN, 0};
  return poll(&pfd, 1, 0) > 0;
}

}  // namespace hornet::net

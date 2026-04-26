#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace hornet::crypto::ecdsa {

enum class DERParseType {
  Lax,
  Strict,
};

namespace detail {

template <typename Wide>
class LaxDERParser {
 public:
  explicit LaxDERParser(std::span<const uint8_t> bytes) : it_(bytes.begin()), end_(bytes.end()) {}

  std::optional<std::pair<Wide, Wide>> ParseSignature() {
    using Signature = std::pair<Wide, Wide>;

    if (!ConsumeTag(0x30)) return std::nullopt;
    if (!ConsumeLength()) return std::nullopt;

    const auto r = ReadInteger();
    if (!r) return std::nullopt;

    const auto s = ReadInteger();
    if (!s) return std::nullopt;

    if (overflow_) return Signature{};
    return Signature{*r, *s};
  }

 private:
  bool ConsumeTag(uint8_t tag) {
    if (it_ == end_ || *it_ != tag) return false;
    ++it_;
    return true;
  }

  bool ConsumeLength() {
    if (it_ == end_) return false;
    const uint8_t length = *it_++;
    if ((length & 0x80) == 0) return true;

    const size_t encoded_bytes = length & 0x7f;
    if (encoded_bytes > size_t(end_ - it_)) return false;
    it_ += encoded_bytes;
    return true;
  }

  std::optional<size_t> ReadLength() {
    if (it_ == end_) return std::nullopt;
    const uint8_t length = *it_++;
    if ((length & 0x80) == 0) return length;

    size_t encoded_bytes = length & 0x7f;
    if (encoded_bytes > size_t(end_ - it_)) return std::nullopt;
    while (encoded_bytes > 0 && *it_ == 0) {
      ++it_;
      --encoded_bytes;
    }
    if (encoded_bytes >= sizeof(size_t)) return std::nullopt;

    size_t value = 0;
    while (encoded_bytes > 0) {
      value = (value << 8) | *it_++;
      --encoded_bytes;
    }
    return value;
  }

  std::optional<Wide> ReadInteger() {
    if (!ConsumeTag(0x02)) return std::nullopt;

    const auto length = ReadLength();
    if (!length || *length > size_t(end_ - it_)) return std::nullopt;

    auto value = std::span{it_, *length};
    it_ += *length;
    while (!value.empty() && value.front() == 0) value = value.subspan(1);

    if (value.size() > sizeof(Wide)) {
      overflow_ = true;
      return Wide{};
    }

    std::array<uint8_t, sizeof(Wide)> bytes{};
    std::copy(value.begin(), value.end(), bytes.begin() + sizeof(Wide) - value.size());
    return Wide::FromBigEndianBytes(bytes);
  }

  typename std::span<const uint8_t>::iterator it_;
  typename std::span<const uint8_t>::iterator end_;
  bool overflow_ = false;
};

template <typename Wide>
class StrictDERParser {
 public:
  explicit StrictDERParser(std::span<const uint8_t> bytes) : it_(bytes.begin()), end_(bytes.end()) {}

  std::optional<std::pair<Wide, Wide>> ParseSignature() {
    using Signature = std::pair<Wide, Wide>;

    if (!ConsumeTag(0x30)) return std::nullopt;

    const auto tuple_length = ReadLength();
    if (!tuple_length || *tuple_length != size_t(end_ - it_)) return std::nullopt;

    const auto r = ReadInteger();
    if (!r) return std::nullopt;

    const auto s = ReadInteger();
    if (!s) return std::nullopt;

    if (it_ != end_) return std::nullopt;
    return Signature{*r, *s};
  }

 private:
  bool ConsumeTag(uint8_t tag) {
    if (it_ == end_ || *it_ != tag) return false;
    ++it_;
    return true;
  }

  std::optional<size_t> ReadLength() {
    if (it_ == end_) return std::nullopt;
    const uint8_t length = *it_++;
    if ((length & 0x80) != 0) return std::nullopt;
    return length;
  }

  std::optional<Wide> ReadInteger() {
    if (!ConsumeTag(0x02)) return std::nullopt;

    const auto length = ReadLength();
    if (!length || *length == 0 || *length > size_t(end_ - it_)) return std::nullopt;

    const auto value = std::span{it_, *length};
    it_ += *length;

    if ((value.front() & 0x80) != 0) return std::nullopt;
    if (value.size() > 1 && value.front() == 0 && (value[1] & 0x80) == 0) return std::nullopt;

    auto magnitude = value;
    if (magnitude.front() == 0) magnitude = magnitude.subspan(1);
    if (magnitude.size() > sizeof(Wide)) return std::nullopt;

    std::array<uint8_t, sizeof(Wide)> bytes{};
    std::copy(magnitude.begin(), magnitude.end(), bytes.begin() + sizeof(Wide) - magnitude.size());
    return Wide::FromBigEndianBytes(bytes);
  }

  typename std::span<const uint8_t>::iterator it_;
  typename std::span<const uint8_t>::iterator end_;
};

}  // namespace detail

template <typename Wide>
std::optional<std::pair<Wide, Wide>> ParseSignatureDER(std::span<const uint8_t> bytes, DERParseType strictness = DERParseType::Lax) {
  if (strictness == DERParseType::Lax)
    return detail::LaxDERParser<Wide>{bytes}.ParseSignature();
  else
    return detail::StrictDERParser<Wide>{bytes}.ParseSignature();
}

}  // namespace hornet::crypto::ecdsa

#pragma once

#include <concepts>
#include <span>
#include <vector>

namespace hornet::util {

// The SubArray class represents a range of values inside an external std::vector<T>.
// This is used to avoid jagged arrays (vectors of vectors), and allocate all memory in
// flat buffers for the benefit of heap fragmentation and cache coherence.
template <typename T, std::integral Count = int>
class SubArray {
 public:
  SubArray() : start_(0), count_(0) {}
  SubArray(int start, Count count) : start_(start), count_(count) {}
  SubArray(std::span<const T> subspan, std::span<const T> data)
      : start_(static_cast<int>(subspan.begin() - data.begin())),
        count_(static_cast<Count>(subspan.size())) {}
  SubArray(const SubArray&) = default;
  SubArray& operator=(const SubArray&) = default;

  int StartIndex() const {
    return start_;
  }

  int EndIndex() const {
    return start_ + count_;
  }

  Count Size() const {
    return count_;
  }

  bool IsEmpty() const {
    return count_ <= 0;
  }

  void Resize(Count count) {
    count_ = count;
  }

  void Offset(int offset) {
    start_ += offset;
  }

  std::span<T> Span(const std::span<T>& data) {
    return data.subspan(start_, count_);
  }

  std::span<const T> Span(const std::span<const T>& data) const {
    return data.subspan(start_, count_);
  }

  std::span<T> Span(std::vector<T>& data) {
    return Span(std::span<T>{data});
  }

  std::span<const T> Span(const std::vector<T>& data) const {
    return Span(std::span<const T>{data});
  }

 private:
  int start_;
  Count count_;
};

}  // namespace hornet::util

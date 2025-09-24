#pragma once

#include <iterator>

namespace hornet::util {

template <typename Iterator, typename End = Iterator>
class IteratorRange {
 public:
  IteratorRange(Iterator begin, End end) : begin_(begin), end_(end) {}

  Iterator begin() const {
    return begin_;
  }
  End end() const {
    return end_;
  }
  size_t size() const {
    return static_cast<size_t>(std::max<ssize_t>(0, std::distance(begin_, end_)));
  }
 private:
  Iterator begin_; 
  End end_;
};

template <typename Iterator, typename End = Iterator>
IteratorRange<Iterator, End> MakeRange(Iterator begin, End end) {
  return {begin, end};
}

}  // namespace hornet::util

// Enable std::ranges methods to operator on rvalue references.
namespace std::ranges {
template <typename I, typename S>
inline constexpr bool enable_borrowed_range<hornet::util::IteratorRange<I, S>> = true;
}  // namespace std::ranges

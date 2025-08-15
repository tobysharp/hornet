#pragma once

namespace hornet::util {

template <typename Iterator>
class IteratorRange {
 public:
  IteratorRange(Iterator begin, Iterator end) : begin_(begin), end_(end) {}

  Iterator begin() const {
    return begin_;
  }
  Iterator end() const {
    return end_;
  }
  size_t size() const {
    return static_cast<size_t>(std::max(0, end_ - begin_));
  }
 private:
  Iterator begin_, end_;
};

template <typename Iterator>
IteratorRange<Iterator> MakeRange(Iterator begin, Iterator end) {
  return {begin, end};
}

}  // namespace hornet::util

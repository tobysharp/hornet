#pragma once

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
    return static_cast<size_t>(std::max(0, end_ - begin_));
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

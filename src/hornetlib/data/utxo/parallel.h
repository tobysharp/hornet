#pragma once

#include <algorithm>

namespace hornet::data::utxo {

template <typename T, typename Fn>
inline void ParallelFor(T begin, T end, Fn&& fn) {
  // TODO
  for (T i = begin; i != end; ++i) fn(i);
}

template <typename T, typename Collection, typename Fn>
inline T ParallelSum(const Collection& collection, const T& initial, Fn&& fn) {
  // TODO
  return std::accumulate(collection.begin(), collection.end(), initial, [&](const T& sum, const auto& element) {
    return sum + fn(element);
  });
}

template <typename Iter>
inline void ParallelSort(Iter begin, Iter end) {
  // TODO: Parallelism.
  std::sort(begin, end);
}

}  // namespace hornet::data::utxo

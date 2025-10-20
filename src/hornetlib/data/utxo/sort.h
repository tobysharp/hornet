#pragma once

#include <algorithm>
#include <vector>

namespace hornet::data::utxo {

template <typename Iter1, typename Iter2>
inline void SortTogether(Iter1 begin, Iter1 end, Iter2 secondary) {
  const auto a = begin;
  const int size = end - begin;
  const auto b = secondary;

  // Generate permutation indices via sort.
  std::vector<int> p(size);
  std::iota(p.begin(), p.end(), 0);
  std::sort(p.begin(), p.end(), [&](int i, int j) { return a[i] < a[j]; });

  // Cycle rotation.
  for (int dst = 0; dst < size; ++dst) {
    if (p[dst] == dst) continue;
    int j = dst, src = p[j], next = p[src];
    const auto ta = std::move(a[src]);
    const auto tb = std::move(b[src]);
    for (; src != dst; j = src, src = next, next = p[src]) {
      a[j] = std::move(a[next]);
      b[j] = std::move(b[next]);
      p[j] = j;
    }
    a[j] = std::move(ta);
    b[j] = std::move(tb);
    p[j] = j;
  }
}

template <typename Iter>
inline void ParallelSort(Iter begin, Iter end) {
  // TODO: Parallelism.
  std::sort(begin, end);
}

}  // namespace hornet::data::utxo

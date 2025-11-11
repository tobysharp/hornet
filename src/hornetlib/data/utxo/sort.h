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
    auto va = std::move(a[dst]);
    auto vb = std::move(b[dst]);

    int j = dst;
    for (int next = p[j]; next != dst; j = next, next = p[j]) {
      a[j] = std::move(a[next]);
      b[j] = std::move(b[next]);
      p[j] = j;       // mark done
    }

    a[j] = std::move(va);
    b[j] = std::move(vb);
    p[j] = j;
  }
}

}  // namespace hornet::data::utxo

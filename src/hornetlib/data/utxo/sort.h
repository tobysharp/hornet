#pragma once

#include <algorithm>
#include <numeric>
#include <tuple>
#include <vector>

namespace hornet::data::utxo {

template <typename Iter1, typename... Iters>
inline void SortTogether(Iter1 begin, Iter1 end, Iters... secondaries) {
  const auto a = begin;
  const int size = end - begin;

  // Generate permutation indices via sort.
  std::vector<int> p(size);
  std::iota(p.begin(), p.end(), 0);
  std::sort(p.begin(), p.end(), [&](int i, int j) { return a[i] < a[j]; });

  // Cycle rotation.
  for (int dst = 0; dst < size; ++dst) {
    if (p[dst] == dst) continue;
    auto va = std::move(a[dst]);
    auto vs = std::make_tuple(std::move(secondaries[dst])...);

    int j = dst;
    for (int next = p[j]; next != dst; j = next, next = p[j]) {
      a[j] = std::move(a[next]);
      ((secondaries[j] = std::move(secondaries[next])), ...);
      p[j] = j;       // mark done
    }

    a[j] = std::move(va);
    std::apply([&](auto&&... v) { ((secondaries[j] = std::move(v)), ...); }, vs);
    p[j] = j;
  }
}

}  // namespace hornet::data::utxo

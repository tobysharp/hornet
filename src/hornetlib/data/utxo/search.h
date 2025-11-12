#pragma once

#include <algorithm>
#include <span>
#include <vector>

namespace hornet::data::utxo {

template <typename RandomIt, typename T>
inline std::pair<RandomIt, RandomIt> GallopingRangeSearch(RandomIt begin, RandomIt end,
                                                          const T& value) {
  RandomIt gallop = begin;
  int step = 1;
  while (gallop < end) {
    const auto& key = *gallop;
    const std::strong_ordering key_wrt_value = key <=> value;
    if (key_wrt_value == std::strong_ordering::greater) break;
    if (key_wrt_value == std::strong_ordering::less) begin = gallop + 1;
    gallop += step;
    step <<= 1;
  }
  if (gallop < end) end = gallop;
  return {begin, end};
}

template <typename RandomIt, typename T>
inline RandomIt GallopingBinarySearch(RandomIt begin, RandomIt end, const T& value,
                                      auto&& order, auto&& match) {
  const auto [lower, upper] =
      GallopingRangeSearch(begin, end, value, order);
  const auto found = BinarySearchFirst(lower, upper, value, order, match);
  return found == upper ? end : found;
}

}  // namespace hornet::data::utxo

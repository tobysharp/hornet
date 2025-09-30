#pragma once

#include <algorithm>
#include <span>
#include <vector>

namespace hornet::data {

template <typename Key, typename Value>
struct KeyValue {
  inline friend std::strong_ordering operator<=>(const KeyValue& lhs, const Key& rhs) {
    return lhs.key <=> rhs;
  }
  inline friend std::strong_ordering operator<=>(const Key& lhs, const KeyValue& rhs) {
    return lhs <=> rhs.key;
  }

  Key key;
  Value value;
};

// Returns the first compact key-value index within [lower, upper) that matches `match`, or -1 if no
// match.
template <typename Iter, typename T, typename Compare>
inline Iter BinarySearchFirst(Iter begin, Iter end, const T& value, Compare&& compare) {
  const auto it = std::lower_bound(begin, end, value, [&](const auto& lhs, const T& rhs) {
    return compare(lhs, rhs) == std::strong_ordering::less;
  });
  if (it == end || compare(*it, value) != std::strong_ordering::equal) return end;
  return it;
}

template <typename RandomIt, typename T, typename Compare>
inline std::pair<RandomIt, RandomIt> GallopingRangeSearch(RandomIt begin, RandomIt end,
                                                          const T& value, Compare&& compare) {
  RandomIt gallop = begin;
  int step = 1;
  while (gallop < end) {
    const auto& key = *gallop;
    const std::strong_ordering key_wrt_value = compare(key, value);
    if (key_wrt_value == std::strong_ordering::greater) break;
    if (key_wrt_value == std::strong_ordering::less) begin = gallop + 1;
    gallop += step;
    step <<= 1;
  }
  if (gallop < end) end = gallop;
  return {begin, end};
}

template <typename RandomIt, typename T, typename Compare>
inline RandomIt GallopingBinarySearch(RandomIt begin, RandomIt end, const T& match,
                                      Compare&& compare) {
  const auto [lower, upper] =
      GallopingRangeSearch(begin, end, match, std::forward<Compare>(compare));
  const auto found = BinarySearchFirst(lower, upper, match, std::forward<Compare>(compare));
  return found == upper ? end : found;
}

// Searches for a sorted range of queries among a sorted range of an index.
// If there is more than one matching key in the index, we always use the first such match.
// Calls visit for each matched key. Returns the number of queries processed and matched
// successfully. Key: auto key(*qbegin); Visit: void visit(output_index, *qbegin, *ibegin);
template <typename QueryIter, typename IndexIter, typename Key, typename Compare, typename Visit>
int ForEachMatchInDoubleSorted(QueryIter qbegin, QueryIter qend, IndexIter ibegin, IndexIter iend,
                               Key&& key, Compare&& compare, Visit&& visit) {
  int rv = 0;
  if (!(qbegin < qend)) return rv;

  // Start by binary searching for the first outpoint in the compact index.
  // From there we will do a galloping search for each next query.
  auto lower = ibegin;
  auto upper = iend;

  // Binary search compact_[begin:end) looking for query_key.match.
  auto search = BinarySearchFirst(lower, upper, key(*qbegin), std::forward<Compare>(compare));
  if (search != upper) visit(rv++, 0, *search);

  // Search the remainder of the compact index using galloping search.
  for (auto query = qbegin + 1; query != qend; ++query) {
    const auto match = key(*query);
    std::tie(lower, upper) =
        GallopingRangeSearch(lower, iend, match, std::forward<Compare>(compare));
    search = BinarySearchFirst(lower, upper, match, std::forward<Compare>(compare));
    if (search != upper) visit(rv++, query - qbegin, *search);
  }
  return rv;
}

}  // namespace hornet::data
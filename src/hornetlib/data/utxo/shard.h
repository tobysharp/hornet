#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "hornetlib/data/utxo/compact_index.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::data {

// A top-level partition of the spent index.
class UnspentShard {
 public:
  UnspentShard(int shard_bits = 9, int directory_bits = 7)
      : shard_bits_(shard_bits),
        directory_bits_(directory_bits),
        directory_((1 << directory_bits) + 1),
        compact_(shard_bits + directory_bits),
        run_indices_(32),
        out_offsets_(32) {}

  using QueryResults = std::expected<std::span<uint64_t>, std::monostate>;

  // Query the unspent shard with the given SORTED query outpoints.
  // If all queries matched, returns a vector of indices into OutputsTable.
  // Otherwise returns std::unexpected.
  QueryResults Query(std::span<const protocol::OutPoint> queries) const {
    QueryResults rv = {};
    if (queries.empty()) return rv;
    run_indices_.resize(queries.size());
    out_offsets_.resize(queries.size());

    const int dir_index = GetDictionaryIndex(queries.front());
    const int lower = directory_[dir_index];
    const int upper = directory_[dir_index + 1];  // Yes, because we deliberately sized it as 2^D+1.

    // Search the compact index for a list of candidates
    const int candidates =
        compact_.Query(queries, run_indices_.data(),
                        run_indices_.size(), lower, upper);

    // All the candidates are already sorted so now go through them, indexing into the full run,
    // and checking for an exact match, building the set of offsets for this shard.
    const int matches_from_candidates = MergeWalkQueriesAndCandidates(queries, run_indices_.data(), candidates);

    // Search the sorted tail separately for all query matches.
    const auto tail = std::span{run_}.subspan(tail_);
    const auto matches_from_tail = ForEachMatchInDoubleSorted(queries.begin(), queries.end(), tail.begin(), tail.end(), 
      [](const protocol::OutPoint& key) { return key; },
      [&](int match_index, const IndexEntry& kv) {
        out_offsets_[matches_from_candidates + match_index] = kv.value;
        return true;
      }
    );
    
    // Now we are in a position to know if any of the matches were missing from the unspent shard index.
    const int total_matches = matches_from_candidates + matches_from_tail;
    Assert(total_matches <= std::ssize(queries));
    if (total_matches < std::ssize(queries)) {
      // Not all queries were found in the unspent index.
      return std::unexpected(std::monostate{});
    }

    return std::span{out_offsets_}.subspan(0, total_matches);
  }

 private:
  struct IndexValue {
    uint64_t spent : 1;     // 0 = unspent, 1 = spent.
    uint64_t offset : 48;   // Byte offset within segment.
    uint64_t segment : 15;  // Segment index in OutputsTable.
  };

  using IndexEntry = KeyValue<protocol::OutPoint, uint64_t>;

  inline std::strong_ordering ComparePrefix(const protocol::OutPoint& query, const IndexEntry& kv) const {
    const uint16_t query_prefix = compact_.KeyPrefix(query);
    const uint16_t index_prefix = compact_.KeyPrefix(kv.key);
    return query_prefix <=> index_prefix;
  }

  int MergeWalkQueriesAndCandidates(const std::span<const protocol::OutPoint> queries, const uint32_t* candidate_begin, int candidates) const {
    // All the candidates are already sorted so now go through them, indexing into the full run,
    // and checking for an exact match, building the set of offsets for this shard.
    int outputs = 0;
    auto query = queries.begin();
    auto candidate = candidate_begin;
    const auto candidates_end = candidate_begin + candidates;
    const auto tail = std::span{run_}.subspan(tail_);
    const auto index_end = tail.begin();
    auto index = candidate != candidates_end ? std::next(run_.begin(), *candidate) : index_end;
    while (query != queries.end() && index != index_end) {
      std::strong_ordering prefix_query_vs_candidate = ComparePrefix(*query, *index);
      if (prefix_query_vs_candidate == std::strong_ordering::equal) {
        // Prefixes are matching, so *index <= *query.
        const auto match_it = GallopingBinarySearch(index, index_end, *query);
        if (match_it == index_end) {
          ++query;
        } else {
          // Found an exact match
          out_offsets_[outputs++] = match_it->value;
          // Move on to next query and next candidate
          ++query;
          if (++candidate == candidates_end) index = index_end;
          else index = std::next(run_.begin(), std::max(*candidate, static_cast<uint32_t>(match_it - run_.begin()) + 1));
        }
      }
      else if (prefix_query_vs_candidate == std::strong_ordering::less) {
        // Query is behind candidate: query has no match here, maybe it's in the tail.
        ++query;
      } else if (prefix_query_vs_candidate == std::strong_ordering::greater) {
        // Candidate is behind query: candidate has no match
        index = ++candidate != candidates_end ? std::next(run_.begin(), *candidate) : index_end;
      }
    }
    return outputs;
  }

  inline uint16_t GetDictionaryIndex(const protocol::OutPoint& out_point) const {
    uint32_t word;
    std::memcpy(&word, out_point.hash.data(), sizeof(word));
    const uint32_t after = word >> shard_bits_;
    const uint32_t directory_mask = (1 << directory_bits_) - 1;
    return after & directory_mask;
  }

  const int shard_bits_;
  const int directory_bits_;
  std::vector<IndexEntry> run_;
  std::vector<uint32_t> directory_;
  CompactIndex compact_;
  size_t tail_;  // Offset to the start of the disjoint sorted tail in run_.
  mutable std::vector<uint32_t> run_indices_;
  mutable std::vector<uint64_t> out_offsets_;
};

}  // namespace hornet::data
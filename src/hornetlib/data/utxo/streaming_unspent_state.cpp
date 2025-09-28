#include <algorithm>
#include <expected>
#include <span>
#include <unordered_map>
#include <vector>

#include "hornetlib/consensus/types.h"
#include "hornetlib/data/utxo/streaming_unspent_state.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/throw.h"

namespace hornet::data {

namespace {

template <typename Key, typename Value>
struct KeyValue {
  inline friend bool operator <(const KeyValue& lhs, const Key& rhs) { return lhs.key < rhs; }
  inline friend bool operator <(const Key& lhs, const KeyValue& rhs) { return lhs < rhs.key; }

  Key key;
  Value value;
};

using OutputRecord = consensus::UnspentDetail;

// Append-only storage for full outputs. Disk-backed, segmented, mmap for reads.
// Copy-merge for compaction in the background, per-segment prioritized by least efficient first.
class OutputsTable {
 public:
  void AppendOutputs(const protocol::Block& block, int height);
  uint64_t AppendOutput(const OutputRecord& record);

  // For each {segment, offset} index in [begin, end), call fn with an OutputRecord.
  template <typename Iter, typename Fn>
  void ForEachOutput(Iter begin, Iter end, Fn&& fn);

 private:
  std::vector<uint8_t> data_;
};

uint64_t OutputsTable::AppendOutput(const OutputRecord& record) {
  const uint64_t start = data_.size();
  const uint8_t* precord = reinterpret_cast<const uint8_t*>(&record);
  const uint8_t* pscript = reinterpret_cast<const uint8_t*>(&record.script);
  data_.insert(data_.end(), precord, pscript);
  data_.push_back(static_cast<uint32_t>(record.script.size()));
  data_.insert(data_.end(), record.script.begin(), record.script.end());
  return start;
}

void OutputsTable::AppendOutputs(const protocol::Block& block, int height) {
  for (const auto& tx : block.Transactions()) {
    for (int i = 0; i < tx.OutputCount(); ++i) {
      const auto& output = tx.Output(i);
      AppendOutput(OutputRecord{height, 0, output.value, tx.PkScript(i)});
    }
  }
}

template <typename RandomIt, typename T>
inline std::pair<RandomIt, RandomIt> GallopingRangeSearch(RandomIt begin, RandomIt end,
                                                          const T& value) {
  RandomIt gallop = begin;
  int step = 1;
  while (gallop < end) {
    const auto& key = *gallop;
    if (value < key) break;
    if (key < value) begin = gallop + 1;
    gallop += step;
    step <<= 1;
  }
  if (gallop < end) end = gallop;
  return {begin, end};
}

// Returns the first compact key-value index within [lower, upper) that matches `match`, or -1 if no
// match.
template <typename Iter, typename T>
inline Iter BinarySearchFirst(Iter begin, Iter end, const T& match) {
  const auto it = std::lower_bound(begin, end, match);
  if (it == end || match < *it) return end;
  return it;
}

// Searches for a sorted range of queries among a sorted range of an index.
// If there is more than one matching key in the index, we always use the first such match.
// Calls visit for each matched key. Returns the number of queries processed and matched successfully.
// Key: auto key(*qbegin);
// Visit: void visit(output_index, *ibegin);
template <typename QueryIter, typename IndexIter, typename Key, typename Visit>
int ForEachMatchInDoubleSorted(QueryIter qbegin, QueryIter qend,
                                         IndexIter ibegin, IndexIter iend, Key key, Visit visit) {
  int rv = 0;
  if (!(qbegin < qend)) return rv;

  // Start by binary searching for the first outpoint in the compact index.
  // From there we will do a galloping search for each next query.
  auto lower = ibegin;
  auto upper = iend;

  // Binary search compact_[begin:end) looking for query_key.match.
  auto search = BinarySearchFirst(lower, upper, key(*qbegin));
  if (search != upper) visit(rv++, *search);

  // Search the remainder of the compact index using galloping search.
  for (auto query = qbegin + 1; query != qend; ++query) {
    const auto match = key(*query);
    std::tie(lower, upper) = GallopingRangeSearch(lower, iend, match);
    search = BinarySearchFirst(lower, upper, match);
    if (search != upper) visit(rv++, *search);
  }
  return rv;
}

class CompactIndex {
 public:
  CompactIndex(int skip_bits) : skip_bits_(skip_bits) {}

  int Query(std::span<const protocol::OutPoint> queries,
                      uint32_t* candidates, const int size, const int lo = 0,
                      const int hi = std::numeric_limits<int>::max()) const {
    // We actually require that the size of the candidates buffer is sufficient to hold one 
    // candidate per query.
    if (size < static_cast<int>(queries.size()))
      util::ThrowInvalidArgument("CompactIndex::Query size of candidates fewer than queries.");

    const auto matcher = [&](const protocol::OutPoint& op) -> uint16_t {
      return KeyPrefix(op);
    };
    const auto visit = [&](int candidate_index, const CompactKeyValue& kv)  {
      // TODO: Debug assert that candidate_index < size, but should be guaranteed by construction.
      candidates[candidate_index] = kv.Value();
    };
    const auto lower = compact_.begin() + std::max(lo, 0);
    const auto upper = std::min(compact_.begin() + hi, compact_.end());
    return ForEachMatchInDoubleSorted(queries.begin(), queries.end(), lower, upper, matcher, visit);
  }

  uint16_t KeyPrefix(const protocol::OutPoint& out_point) const {
    constexpr int kTxidBits = 12;
    constexpr int kVoutBits = 4;

    uint32_t word;
    std::memcpy(&word, out_point.hash.data(), sizeof(word));

    uint32_t after = word >> skip_bits_;
    uint16_t txid_part = after & ((1 << kTxidBits) - 1);
    uint16_t vout_part = out_point.index & ((1 << kVoutBits) - 1);
    return (txid_part << kVoutBits) | vout_part;
  }

 private:
  class CompactKeyValue {
   public:
    CompactKeyValue(uint16_t key, uint32_t value)
        : storage_((key << kKeyBits) + (value & kValueMask)) {}
    CompactKeyValue(uint32_t storage) : storage_(storage) {}

    uint16_t Key() const {
      return static_cast<uint16_t>(storage_ >> kValueBits);
    }
    uint32_t Value() const {
      return storage_ & kValueMask;
    }
    uint32_t Storage() const {
      return storage_;
    }
    friend std::strong_ordering operator<=>(CompactKeyValue lhs, uint16_t rhs) {
      return lhs.Key() <=> rhs;
    }
    friend std::strong_ordering operator<=>(uint16_t lhs, CompactKeyValue rhs) {
      return lhs <=> rhs.Key();
    }
    static uint16_t MaximumKey() {
      return static_cast<uint16_t>((1 << kKeyBits) - 1);
    }
    static uint32_t MaximumValue() {
      return (1 << kValueBits) - 1;
    }

   private:
    static constexpr int kKeyBits = 13;
    static constexpr int kValueBits = 19;
    static constexpr int kValueMask = (1 << kValueBits) - 1;
    uint32_t storage_;
  };

  const int skip_bits_;
  std::vector<CompactKeyValue> compact_;
};

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
      [&](int output_index, const IndexEntry& kv) {
        out_offsets_[matches_from_candidates + output_index] = kv.value;
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
      while (prefix_query_vs_candidate == std::strong_ordering::equal) {
        // Prefixes are matching: compare the whole key.
        const std::strong_ordering order = *query <=> index->key;
        if (order == std::strong_ordering::equal) {
          // Found an exact match
          out_offsets_[outputs++] = index->value;
          // Move on to next query and next candidate
          ++query;
          index = ++candidate != candidates_end ? std::next(run_.begin(), *candidate) : index_end;
          break;
        } else if (order == std::strong_ordering::less) {
          // Query is behind index. Query has no match here, move to next query.
          ++query;
          break;
        } else if (order == std::strong_ordering::greater) {
          // Index is behind query: continue scanning index while prefix matches.
          if (++index == index_end) break;
          prefix_query_vs_candidate = ComparePrefix(*query, *index);
        } 
      }
      if (prefix_query_vs_candidate == std::strong_ordering::less) {
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

class UnspentIndex {
 public:
  UnspentIndex(int shard_bits = 9, int dictionary_bits = 7) {
    shards_.reserve(1 << shard_bits);
    for (int i = 0; i < 1 << shard_bits; ++i)
      shards_.emplace_back(shard_bits, dictionary_bits);
  }

  int ShardCount() const {
    return std::ssize(shards_);
  }

  // Query all the input prevouts to check they exist as unspent outputs.
  void QueryUnspent(const protocol::Block&) {}

 protected:
  std::vector<UnspentShard> shards_;
};

}  // namespace

class StreamingUnspentState::Impl {
 public:
  struct Info {};

  // Retrieve stats on the internal state of this object.
  Info GetInfo() const;

  // Mark input prevouts as spent and add new outputs.
  // May use cached mutable state from the previous EnumerateUnspentImpl to save duplicated work.
  void ConnectBlock(const protocol::Block& block);

  // Explicitly compact the representation, which may be an expensive operation.
  void Compact();

  template <typename Fn>
  consensus::Result QueryUnspent(const protocol::Block&, Fn&&) {
    return {};
  }

 private:
  OutputsTable outputs_;
  UnspentIndex unspent_;
};

consensus::Result StreamingUnspentState::EnumerateUnspentImpl(const protocol::Block& block,
                                                              const Callback cb,
                                                              const void* user) const {
  return impl_->QueryUnspent(block, [&](int tx_index, int input_index, const OutputRecord& record) {
    return (*cb)(tx_index, input_index, record, user);
  });
}

}  // namespace hornet::data
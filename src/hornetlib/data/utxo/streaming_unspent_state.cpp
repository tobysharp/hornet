#include <algorithm>
#include <expected>
#include <span>
#include <unordered_map>
#include <vector>

#include "hornetlib/consensus/types.h"
#include "hornetlib/data/utxo/streaming_unspent_state.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::data {

namespace {

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
inline std::pair<RandomIt, RandomIt> GallopingRangeSearch(RandomIt begin, RandomIt end, const T& value) {
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

struct SearchResults {
  bool missing;    // True if any of the processed queries were not found in the unspent index.
                    // (Failure to validate spending.)
  bool overflow;   // True if we stopped the query early because we filled the candidates buffer.
  int processed;   // Number of queries processed and candidates written to output buffer.
};

template <typename IndexIter, typename Visit>
SearchResults ProcessCandidate(IndexIter in, IndexIter end, SearchResults acc, Visit visit) {
  if ((acc.missing |= (in == end))) return acc;
  const bool is_continue = visit(acc.processed++, *in);
  if ((acc.overflow |= !is_continue)) return acc;
  return acc;
}

// Returns the first compact key-value index within [lower, upper) that matches `match`, or -1 if no match.
template <typename Iter, typename T>
inline Iter BinarySearchFirst(Iter begin, Iter end, const T& match) {
  const auto it = std::lower_bound(begin, end, match);
  if (it == end || match < *it) return end;
  return it;
}

template <typename QueryIter, typename Key, typename IndexIter, typename Visit>
SearchResults ForEachMatchInDoubleSorted(QueryIter qbegin, QueryIter qend, Key key, 
                                         IndexIter ibegin, IndexIter iend, Visit visit) {
    SearchResults rv = {false, false, 0 };
    if (!(qbegin < qend)) return rv;

    // Start by binary searching for the first outpoint in the compact index.
    // From there we will do a galloping search for each next query.
    auto lower = ibegin;
    auto upper = iend;
    
    // Binary search compact_[begin:end) looking for query_key.match.
    auto search = BinarySearchFirst(lower, upper, key(*qbegin));
    rv = ProcessCandidate(search, upper, rv, visit);
    if (rv.overflow || rv.missing) return rv;

    // Search the remainder of the compact index using galloping search.
    for (auto query = qbegin + 1; query != qend; ++query) {
      const auto match = key(*query);
      std::tie(lower, upper) = GallopingRangeSearch(lower, iend, match);
      search = BinarySearchFirst(lower, upper, match);
      rv = ProcessCandidate(search, upper, rv, visit);
      if (rv.overflow || rv.missing) return rv;
    }
    return rv;
}

class CompactIndex {
 public:
  SearchResults Query(std::span<const protocol::OutPoint> queries, const int skip_bits,
                      uint32_t* candidates, const int size, const int lo = 0,
                      const int hi = std::numeric_limits<int>::max()) const {

    // Start by binary searching for the first outpoint in the compact index.
    // From there we will do a galloping search for each next query.
    const auto matcher = [&](const protocol::OutPoint& op) -> uint16_t {
      return GetMatchBits(op, skip_bits);
    };
    const auto visit = [&](int candidate_index, const CompactKeyValue& kv) -> bool {
      candidates[candidate_index] = kv.Value();
      return candidate_index < size;
    };
    auto lower = compact_.begin() + std::max(lo, 0);
    auto upper = std::min(compact_.begin() + hi, compact_.end());
    return ForEachMatchInDoubleSorted(queries.begin(), queries.end(), matcher, lower, upper, visit);
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
    friend std::strong_ordering operator <=>(CompactKeyValue lhs, uint16_t rhs) {
      return lhs.Key() <=> rhs;
    }
    friend std::strong_ordering operator <=>(uint16_t lhs, CompactKeyValue rhs) {
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

  inline uint16_t GetMatchBits(const protocol::OutPoint& out_point, int skip_bits) const {
    constexpr int kTxidBits = 12;
    constexpr int kVoutBits = 4;

    uint32_t word;
    std::memcpy(&word, out_point.hash.data(), sizeof(word));

    uint32_t after = word >> skip_bits;
    uint16_t txid_part = after & ((1 << kTxidBits) - 1);
    uint16_t vout_part = out_point.index & ((1 << kVoutBits) - 1);
    return (txid_part << kVoutBits) | vout_part;
  }

  std::vector<CompactKeyValue> compact_;
};

// A top-level partition of the spent index.
class UnspentShard {
 public:
  UnspentShard(int directory_bits = 7)
      : directory_bits_(directory_bits), directory_((1 << directory_bits) + 1),
      run_indices_(32), out_offsets_(32) {
  }

  struct QueryResults {
    std::span<uint64_t> outputs_refs;
  };

  // Query the unspent shard with the given SORTED query outpoints.
  // If all queries matched, returns a vector of indices into OutputsTable.
  // Otherwise returns std::unexpected.
  QueryResults Query(std::span<const protocol::OutPoint> queries, int shard_bits) const {
    QueryResults rv = {};
    if (queries.empty()) return rv;

    int queries_processed = 0;
    while (queries_processed < std::ssize(queries)) {
      // Start by looking up the query in our radix-style dictionary to get an initial bounded range.
      const auto query_batch = queries.subspan(queries_processed);
      const int dir_index = GetDictionaryIndex(query_batch.front(), shard_bits);
      const int lower = directory_[dir_index];
      const int upper = directory_[dir_index + 1];  // Yes, because we deliberately sized it as 2^D+1.

      // Search the compact index for a list of candidates
      const auto candidate_batch = std::span{run_indices_};
      const auto results = compact_.Query(query_batch, shard_bits + directory_bits_,
                                            candidate_batch.data(), candidate_batch.size(), lower, upper);
      if (results.missing) {
        // TODO: Return with missing unspent record error
      } else if (results.overflow) {
        // We ran out of space in the candidates buffer. We'll double its size to prevent this happening again.
        // We'll have to run the query tail again, and hopefully we don't run out of memory again.
        // We do rely on having enough memory for these candidates, but the expected number is small, ~6 per shard per block, or 12KB total.
        run_indices_.resize(run_indices_.size() << 1);
        // We'll have to run the query tail again, and hopefully we don't run out of memory again.
        // TODO: Put a policy on this to bound the maximum size
        // Double the size of the out_offsets buffer too since that has to contain about the same number of entries.
        out_offsets_.reserve(out_offsets_.capacity() << 1);
      }

      // All the candidates are already sorted so now go through them, indexing into the full run,
      // and checking for an exact match, building the set of offsets for this shard.
      for (int i = 0; i < results.processed; ++i) {
        std::strong_ordering order = std::strong_ordering::less;
        for (uint32_t run_index = candidate_batch[i]; run_index < tail_ && order == std::strong_ordering::less; ++run_index) {
          const KeyValue& run_entry = run_[run_index];
          order = run_entry.key <=> query_batch[i];
          if (order == std::strong_ordering::equal) {
            // We have an exact match. This is the very common case.
            out_offsets_.push_back(run_entry.value);
            // Since the outpoints are all unique, we can move onto the next query now.
          }
        }
        if (order != std::strong_ordering::equal) {
          // We did not find an exact match. This implies an unspendable prevout. Early exit here.
          return {}; // TODO
        }
      }
      queries_processed += results.processed;
    }

    // Search the sorted tail separately for all query matches.
    const auto tail = std::span{run_}.subspan(tail_);

    for (size_t i = 0; i < tail.size(); ++i) {
      // TODO: Reuse the galloping binary search from earlier.
      //GallopingRangeSearch(tail.begin(), tail.end(), 
    }

    // TODO: Prepare output value

    return rv;
  }

 private:
  struct IndexValue {
    uint64_t spent : 1;     // 0 = unspent, 1 = spent.
    uint64_t offset : 48;   // Byte offset within segment.
    uint64_t segment : 15;  // Segment index in OutputsTable.
  };

  struct KeyValue {
    protocol::OutPoint key;
    IndexValue value;
  };

  inline uint16_t GetDictionaryIndex(const protocol::OutPoint& out_point, int skip_bits) const {
    uint32_t word;
    std::memcpy(&word, out_point.hash.data(), sizeof(word));
    const uint32_t after = word >> skip_bits;
    const uint32_t directory_mask = (1 << directory_bits_) - 1;
    return after & directory_mask;
  }

  const int directory_bits_;
  std::vector<KeyValue> run_;
  std::vector<uint32_t> directory_;
  CompactIndex compact_;
  size_t tail_;  // Offset to the start of the disjoint sorted tail in run_.
  mutable std::vector<uint32_t> run_indices_;
  mutable std::vector<IndexValue> out_offsets_;
};

class UnspentIndex {
 public:
  UnspentIndex(int shard_bits = 9) : shard_bits_(shard_bits), shards_(1 << shard_bits) {}

  int ShardCount() const {
    return std::ssize(shards_);
  }

  // Query all the input prevouts to check they exist as unspent outputs.
  void QueryUnspent(const protocol::Block&) {}

 protected:
  const int shard_bits_;
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
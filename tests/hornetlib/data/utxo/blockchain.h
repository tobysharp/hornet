#pragma once

#include <array>
#include <random>
#include <unordered_set>
#include <vector>

#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/script/writer.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/log.h"

namespace hornet::test {

// Represents a chain of blocks for the purpose of testing the UTXO database functionality only.
class Blockchain {
 public:
  struct Spend {
    protocol::OutPoint prevout;
    int height;
    int64_t amount;
  };
  
  bool Empty() const { return blocks_.empty(); }
  int Length() const { return std::ssize(blocks_); }
  const protocol::Block& operator[](int index) const { return blocks_[index]; }
  void Append(protocol::Block&& block);
  int UnspentSize() const { return std::ssize(unspent_); }
  const Spend& Unspent(int index) const { return unspent_[index]; }
  int SpentSize() const { return std::ssize(spent_); }
  const Spend& Spent(int index) const { return spent_[index]; }

  protocol::Block Sample(int max_transactions = 1'000, int max_fan_in = 2, int max_fan_out = 4) const;

  static Blockchain Generate(int length, int transactions_per_block = 1'000, int max_fan_in = 2, int max_fan_out = 4);

 private:
  std::vector<int> SampleWithoutReplacement(int count, int end) const;

  mutable std::mt19937 rng_;
  std::vector<Spend> unspent_;
  std::vector<Spend> spent_;
  std::vector<protocol::Block> blocks_;
};

// Add a simulated block to the chain.
inline void Blockchain::Append(protocol::Block&& block) {
  const int height = Length();
  std::vector<int> spent_indices;

  for (const auto tx : block.Transactions()) {
    for (const auto& input : tx.Inputs()) {
      if (input.previous_output.IsNull()) continue;  // Coinbase transaction.
      const int unspent_index = input.sequence;
      const Spend& spend = unspent_[unspent_index];
      Assert(input.previous_output == spend.prevout);
      // LogDebug() << "Spending {" << spend.prevout.hash << ", " << spend.prevout.index << "} at height " << height << " from height " << spend.height;
      spent_.push_back(spend);
      spent_indices.push_back(unspent_index);
    }
    const auto& txid = tx.GetHash();
    for (int i = 0; i < tx.OutputCount(); ++i) {
      unspent_.push_back({{txid, static_cast<uint32_t>(i)}, height, tx.Output(i).value});      
      // LogDebug() << "Funding {" << unspent_.back().prevout.hash << ", " << unspent_.back().prevout.index << "} at height " << height;
    }
  }

  std::sort(spent_indices.begin(), spent_indices.end(), std::greater<int>{});
  Assert(std::adjacent_find(spent_indices.begin(), spent_indices.end()) == spent_indices.end());  // No duplicates.
  for (int unspent_index : spent_indices) {
    unspent_[unspent_index] = unspent_.back();
    unspent_.pop_back();
  }

  blocks_.emplace_back(std::move(block));
}

// Add a simulated block to the chain.
inline protocol::Block Blockchain::Sample(int max_transactions /* = 1000 */, int max_fan_in /* = 2 */, int max_fan_out /* = 4 */) const {
  constexpr int64_t kBlockReward = 50ll * 100'000'000;
  constexpr std::array<uint8_t, 24> pk_script = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18
  };

  protocol::Block block;
  int funded_size = std::ssize(unspent_);  // The number of unspent outputs prior to the appending block.

  std::vector<int> input_counts;
  input_counts.reserve(max_transactions);
  {
    // Add a coinbase transaction.
    protocol::Transaction coinbase;
    coinbase.ResizeInputs(1);
    coinbase.ResizeOutputs(1);
    coinbase.Input(0).previous_output = protocol::OutPoint::Null();
    coinbase.SetSignatureScript(0, protocol::script::Writer{}.PushInt(Length()).Release());
    coinbase.Output(0).value = kBlockReward;
    coinbase.SetPkScript(0, pk_script);
    input_counts.push_back(1);
    block.AddTransaction(coinbase);
  }

  std::uniform_int_distribution<int> in_dist(1, std::max(1, max_fan_in));
  std::uniform_int_distribution<int> out_dist(1, std::max(1, max_fan_out));

  int total_spends = 0;
  for (int ti = 1; ti < max_transactions; ++ti)
  {
    if (funded_size == 0) break;  // Nothing left to spend.
    input_counts.push_back(std::min<int>(in_dist(rng_), funded_size));
    funded_size -= input_counts.back();
    total_spends += input_counts.back();
  }
  
  std::vector<int> unspent_indices = SampleWithoutReplacement(total_spends, std::ssize(unspent_));
  auto unspent_cursor = unspent_indices.begin();
  int transactions = std::min<int>(max_transactions, std::ssize(input_counts));

  // Add other transactions to the block.
  for (int ti = 1; ti < transactions; ++ti) {
    protocol::Transaction tx;
    int input_count = input_counts[ti];
    tx.ResizeInputs(input_count);
    tx.ResizeOutputs(out_dist(rng_));

    int64_t total = 0;
    for (int i = 0; i < input_count; ++i) {
      // Choose a prior output to spend as this input.
      Assert(unspent_cursor != unspent_indices.end());
      int unspent_index = *unspent_cursor++;
      const Spend& spend = unspent_[unspent_index];
      tx.Input(i).previous_output = spend.prevout;
      tx.Input(i).sequence = unspent_index;
      total += spend.amount;
    }

    for (int i = 0; i < tx.OutputCount(); ++i) {
      int64_t amount = total / (tx.OutputCount() - i);
      tx.Output(i).value = amount;
      tx.SetPkScript(i, pk_script);
      total -= amount;
    }

    block.AddTransaction(tx);
  }
  return block;
}

inline std::vector<int> Blockchain::SampleWithoutReplacement(int count, int end) const {
  std::unordered_set<int> s;
  s.reserve(count * 2);

  for (int j = end - count; j < end; ++j) {
    int t = std::uniform_int_distribution<int>(0, j)(rng_);
    if (!s.insert(t).second) s.insert(j);
  }

  return std::vector<int>{s.begin(), s.end()};
}

/* static */ Blockchain Blockchain::Generate(int length, int transactions_per_block /* = 1'000 */, int max_fan_in /* = 2 */, int max_fan_out /* = 4 */) {
  Blockchain chain;
  for (int i = 0; i < length; ++i)
    chain.Append(chain.Sample(transactions_per_block, max_fan_in, max_fan_out));
  return chain;
}

}  // namespace hornet::test

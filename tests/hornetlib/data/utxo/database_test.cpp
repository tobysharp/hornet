#include "hornetlib/data/utxo/database.h"

#include <array>
#include <chrono>
#include <random>
#include <span>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "hornetlib/data/utxo/sort.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/script/writer.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/log.h"
#include "testutil/temp_folder.h"

namespace hornet::data::utxo {
namespace {

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
  const protocol::Block& Append(int max_transactions = 1'000, int max_fan_in = 2, int max_fan_out = 4);
  int UnspentSize() const { return std::ssize(unspent_); }
  const Spend& Unspent(int index) const { return unspent_[index]; }
  
  static Blockchain Generate(int length, int transactions_per_block = 1'000, int max_fan_in = 2, int max_fan_out = 4);

 private:
  std::mt19937 rng_;
  std::vector<Spend> unspent_;
  std::vector<protocol::Block> blocks_;
};

const protocol::Block& Blockchain::Append(int max_transactions /* = 1000 */, int max_fan_in /* = 2 */, int max_fan_out /* = 4 */) {
  constexpr int64_t kBlockReward = 50ll * 100'000'000;
  constexpr std::array<uint8_t, 24> pk_script = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18
  };

  protocol::Block block;
  int unspent_size = std::ssize(unspent_);
  const int height = Length();

  {
    protocol::Transaction coinbase;
    coinbase.ResizeInputs(1);
    coinbase.ResizeOutputs(1);
    coinbase.Input(0).previous_output = protocol::OutPoint::Null();
    coinbase.SetSignatureScript(0, protocol::script::Writer{}.PushInt(Length()).Release());
    coinbase.Output(0).value = kBlockReward;
    unspent_.push_back({{coinbase.GetHash(), 0u}, height, kBlockReward});
    block.AddTransaction(coinbase);
  }

  std::uniform_int_distribution<int> in_dist(1, std::max(1, max_fan_in));
  std::uniform_int_distribution<int> out_dist(1, std::max(1, max_fan_out));

  for (int ti = 1; ti < max_transactions; ++ti) {
    if (unspent_size == 0) break;  // Nothing left to spend from.

    protocol::Transaction tx;
    int input_count = std::min<int>(in_dist(rng_), unspent_size);
    tx.ResizeInputs(input_count);
    tx.ResizeOutputs(out_dist(rng_));

    int64_t total = 0;
    for (int i = 0; i < tx.InputCount(); ++i) {
      int unspent_index = std::uniform_int_distribution<int>(0, unspent_size - 1)(rng_);
      const Spend& spend = unspent_[unspent_index];
      tx.Input(i).previous_output = spend.prevout;
      total += spend.amount;
      unspent_[unspent_index] = unspent_.back();
      unspent_.pop_back();
      --unspent_size;
    }

    for (int i = 0; i < tx.OutputCount(); ++i) {
      int64_t amount = total / (tx.OutputCount() - i);
      tx.Output(i).value = amount;
      tx.SetPkScript(i, pk_script);
      total -= amount;
    }
    for (int i = 0; i < tx.OutputCount(); ++i)
      unspent_.push_back({{tx.GetHash(), static_cast<uint32_t>(i)}, height, tx.Output(i).value});

    block.AddTransaction(tx);
  }

  blocks_.emplace_back(std::move(block));
  return blocks_.back();
}

/* static */ Blockchain Blockchain::Generate(int length, int transactions_per_block /* = 1'000 */, int max_fan_in /* = 2 */, int max_fan_out /* = 4 */) {
  Blockchain chain;
  for (int i = 0; i < length; ++i)
    chain.Append(transactions_per_block, max_fan_in, max_fan_out);
  return chain;
}

TEST(DatabaseTest, TestAppendGenesis) {
  test::TempFolder dir;
  Database database{dir.Path()};
  database.Append(protocol::Block::Genesis(), 0);
}

TEST(DatabaseTest, TestAppendMutableSerial) {
  // Generates a test chain.
  constexpr int kBlocks = 16;
  Blockchain chain = Blockchain::Generate(kBlocks, 100);

  // Constructs a UTXO database.
  test::TempFolder dir;
  Database database{dir.Path()};
  database.SetMutableWindow(kBlocks);

  // Appends the blocks to the database, serially in order.
  for (int i = 0; i < chain.Length(); ++i)
    database.Append(chain[i], i);
  
  {
    // Query for the first block's coinbase output, which should still exist at height 1.
    std::vector<OutputKey> keys(1, {chain[0].Transaction(0).GetHash(), 0u});
    std::vector<OutputId> rids(1, kNullOutputId);
    int query = database.Query(keys, rids, 1);
    EXPECT_EQ(query, 1);
    EXPECT_NE(rids[0], kNullOutputId);
  }

  {
    // Queries for all unspent outputs at the chain tip.
    std::vector<OutputKey> keys(chain.UnspentSize());
    std::vector<OutputId> rids(keys.size(), kNullOutputId);
    for (int i = 0; i < std::ssize(keys); ++i) keys[i] = chain.Unspent(i).prevout;
    database.SortKeys(keys);
    int query = database.Query(keys, rids, chain.Length());
    EXPECT_EQ(query, chain.UnspentSize());
    for (int i = 0; i < std::ssize(keys); ++i)
      EXPECT_NE(rids[i], kNullOutputId);

    // Fetches all unspent output data.
    std::vector<OutputDetail> outputs(rids.size());
    std::vector<uint8_t> scripts;
    database.SortIds(rids);
    int fetched = database.Fetch(rids, outputs, &scripts);
    EXPECT_EQ(fetched, std::ssize(rids));
    EXPECT_LE(scripts.size(), 24u * fetched);
  
    // Verify the conservation of value of all unspent transactions.
    int64_t total = 0;
    for (const OutputDetail& detail : outputs) total += detail.header.amount;
    EXPECT_EQ(total, kBlocks * 50ll * 100'000'000);
  }

  // Try searching for a key that should have been deleted in block 1.
  if (kBlocks > 1) {
    std::vector<OutputKey> keys(1, {chain[0].Transaction(0).GetHash(), 0u});
    std::vector<OutputId> rids(1, kNullOutputId);
    int query = database.Query(keys, rids, 2);
    EXPECT_EQ(query, 0);
    EXPECT_EQ(rids[0], kNullOutputId);
  }
}

TEST(DatabaseTest, TestAppendGeneratedParallel) {
  // Generates a test chain.
  constexpr int kBlocks = 100;
  Blockchain chain = Blockchain::Generate(kBlocks);

  // Constructs a UTXO database.
  test::TempFolder dir;
  Database database{dir.Path()};

  // Appends the blocks to the database.
  ParallelFor(0, chain.Length(), [&](int i) {
    database.Append(chain[i], i);
  });
}

}  // namespace
}  // namespace hornet::data::utxo

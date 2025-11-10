#include "hornetlib/data/utxo/database.h"

#include <array>
#include <chrono>
#include <random>
#include <span>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/log.h"
#include "testutil/temp_folder.h"

namespace hornet::data::utxo {
namespace {

class Blockchain {
 public:
  bool Empty() const { return blocks_.empty(); }
  int Length() const { return std::ssize(blocks_); }
  const protocol::Block& operator[](int index) const { return blocks_[index]; }
  const protocol::Block& Append(int max_transactions = 1000);

 private:
  struct Spend {
    protocol::OutPoint prevout;
    int64_t amount;
  };
  std::mt19937 rng_;
  std::vector<Spend> unspent_;
  std::vector<protocol::Block> blocks_;
};

const protocol::Block& Blockchain::Append(int max_transactions /* = 1000 */) {
  constexpr int kMaxFanIn = 2;
  constexpr int kMaxFanOut = 4;
  constexpr int64_t kBlockReward = 50ll * 100'000'000;
  constexpr std::array<uint8_t, 24> pk_script = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18
  };

  protocol::Block block;
  int unspent_size = std::ssize(unspent_);

  {
    protocol::Transaction coinbase;
    coinbase.ResizeInputs(1);
    coinbase.ResizeOutputs(1);
    coinbase.Input(0).previous_output = protocol::OutPoint::Null();
    coinbase.Output(0).value = kBlockReward;
    unspent_.push_back({{coinbase.GetHash(), 0u}, kBlockReward});
    block.AddTransaction(coinbase);
  }

  std::uniform_int_distribution<int> in_dist(1, kMaxFanIn);
  std::uniform_int_distribution<int> out_dist(1, kMaxFanOut);

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
      unspent_.push_back({{tx.GetHash(), static_cast<uint32_t>(i)}, amount});
      total -= amount;
    }

    block.AddTransaction(tx);
  }

  blocks_.emplace_back(std::move(block));
  return blocks_.back();
}

TEST(DatabaseTest, TestAppendGenesis) {
  test::TempFolder dir;
  Database database{dir.Path()};
  database.Append(protocol::Block::Genesis(), 0);
}

TEST(DatabaseTest, TestAppendGenerated) {
  // Generates a test chain.
  constexpr int kBlocks = 100;
  Blockchain chain;
  for (int i = 0; i < kBlocks; ++i)
    chain.Append();

  // Constructs a UTXO database.
  test::TempFolder dir;
  Database database{dir.Path()};

  // Appends the blocks to the database.
  const auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < chain.Length(); ++i)
    database.Append(chain[i], i);
  const auto stop = std::chrono::high_resolution_clock::now();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
  LogInfo() << "Duration " << ms.count() << " ms";
}

}  // namespace
}  // namespace hornet::data::utxo

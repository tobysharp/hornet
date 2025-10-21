#include <chrono>
#include <thread>
#include <filesystem>
#include <vector>

#include <gtest/gtest.h>

#include "hornetlib/protocol/transaction.h"

namespace hornet::data::utxo {
namespace {

// TEST(UTXOTest, CreateOutputsTable) {
//   auto tmp = std::filesystem::temp_directory_path();
//   auto file = tmp / "hornet_utxo";
//   OutputsTable table{file.string()};

//   protocol::Transaction tx;
//   tx.ResizeInputs(1);
//   tx.ResizeOutputs(1);
//   tx.Input(0).previous_output = protocol::OutPoint::Null();
//   tx.Output(0).value = 1'000'000;  // 1 million sats
//   tx.SetPkScript(0, std::vector<uint8_t>{0x51});
//   IndexEntry entry = table.AppendOutput(tx, 0, 0);

//   EXPECT_EQ(entry.key.hash, tx.GetHash());
//   EXPECT_EQ(entry.key.index, 0);

//   std::this_thread::sleep_for(std::chrono::seconds{1});
// }

}  // namespace
}  // namepsace hornet::data::utxo

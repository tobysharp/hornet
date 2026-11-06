#include "hornetlib/data/utxo/database.h"

#include <algorithm>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "hornetlib/encoding/reader.h"
#include "hornetlib/protocol/block.h"
#include "testutil/temp_folder.h"
#include "testutil/transaction.h"

namespace hornet::data::utxo {
namespace {

using hornet::test::MakeCoinbaseLikeTransaction;
using hornet::test::MakeSpendTransaction;

// Mainnet blocks 91812 and 91842. Both carry the coinbase
// d5d27987d2a3dfc724e359870c6644b40e497bdc0589a033220fe15429d88599; the second is exempt from BIP30 (Table 2) and
// overwrites the first's output in Core's coin set.
constexpr std::string_view kBlock91812 =
    "010000000f362bdc16f2f880097c71fd3296c01b835c8b034e4d2939e8af02000000000065a62f2f6b9102d6eb5eee95"
    "be5ec3fcdfa27cf2117deeebefc6be53761d99499423e04c56720e1bb4518a4502010000000100000000000000000000"
    "00000000000000000000000000000000000000000000ffffffff060456720e1b00ffffffff0100f2052a010000004341"
    "046896ecfc449cb8560594eb7f413f199deb9b4e5d947a142e7dc7d2de0b811b8e204833ea2a2fd9d4c7b153a8ca7661"
    "d0a0b7fc981df1f42f55d64b26b3da1e9cac000000000100000002f11414b9f94e659d9523fa15c8d29bc0d4b52b0965"
    "c431acc3081df1a5118bf9000000008a473044022068b2a587ec9374325c17c79f9102358fdd0574eeb3bebea08affab"
    "3e07afd93c0220248ac15de4e2b6391ff12c348ba6cc024448dba20611a40a35f8b67b6f966cd50141046d337916441e"
    "20c469623303b47fd0c23326956a183e3274801f1a863e04b7f1dc2bee3388292571955955f8a24445ae35781dd9e39e"
    "8472bff623d3375f490effffffffb37c2f3a28b138a8ff4f04037e83f1e5d2af023fee64c5ecd21dbd9f69264d0c0000"
    "00008a47304402206ccc7e10f33ed2e73c000780e972c479cf0a74d4a5825d74eebb8ec87d31da3202207a945ff0062c"
    "9df5d7a9c51483f0d8fc9d0cad1670319c8fbebde2198f8eea2201410450a6524d0f7519571e8fb761ac8285f88c5ad9"
    "976454ee2148db5e0d13caa534f6bc56678a328dab8319fd62feabc977e478776c9cf0e705575be61dccdf8383ffffff"
    "ff0100943577000000001976a914dda4521a9cd99e92323ae4762467dab928b65f4588ac00000000";

constexpr std::string_view kBlock91842 =
    "0100000020114beba985ecbf1351bf5f4d5495105dc2ca94f5bdbcac921e0a00000000009985d82954e10f2233a08905"
    "dc7b490eb444660c8759e324c7dfa3d28779d2d5f34ee04c56720e1b021838e101010000000100000000000000000000"
    "00000000000000000000000000000000000000000000ffffffff060456720e1b00ffffffff0100f2052a010000004341"
    "046896ecfc449cb8560594eb7f413f199deb9b4e5d947a142e7dc7d2de0b811b8e204833ea2a2fd9d4c7b153a8ca7661"
    "d0a0b7fc981df1f42f55d64b26b3da1e9cac00000000";

std::vector<uint8_t> FromHex(std::string_view hex) {
  const auto nibble = [](char c) -> uint8_t { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
  std::vector<uint8_t> bytes;
  bytes.reserve(hex.size() / 2);
  for (size_t i = 0; i + 1 < hex.size(); i += 2) bytes.push_back(nibble(hex[i]) << 4 | nibble(hex[i + 1]));
  return bytes;
}

protocol::Block Decode(std::string_view hex) {
  const auto bytes = FromHex(hex);
  encoding::Reader reader{bytes};
  protocol::Block block;
  block.Deserialize(reader);
  return block;
}

TEST(DuplicateCoinbaseTest, RepeatedCoinbaseSupersedesEarlierOutput) {
  test::TempFolder dir;
  Database db{dir.Path()};

  const protocol::Block first = Decode(kBlock91812);
  const protocol::Block repeat = Decode(kBlock91842);
  ASSERT_EQ(first.Transaction(0).GetHash(), repeat.Transaction(0).GetHash());
  db.Append(first, 91812);
  db.Append(repeat, 91842);

  const std::vector<OutputKey> keys{{repeat.Transaction(0).GetHash(), 0u}};
  std::vector<OutputId> rids(1, kNullOutputId);
  ASSERT_EQ(db.Query(keys, rids, 91843), 1);

  std::vector<OutputDetail> outputs(1);
  std::vector<uint8_t> scripts;
  ASSERT_EQ(db.Fetch(rids, outputs, &scripts), 1);
  EXPECT_EQ(outputs[0].header.height, 91842);

  // Before the repeat, the earlier output stands alone.
  std::fill(rids.begin(), rids.end(), kNullOutputId);
  EXPECT_EQ(db.Query(keys, rids, 91842), 1);
}

TEST(DuplicateCoinbaseTest, SpendOfRepeatedCoinbaseLeavesNothingUnspent) {
  test::TempFolder dir;
  Database db{dir.Path()};

  const protocol::Block first = Decode(kBlock91812);
  const protocol::Block repeat = Decode(kBlock91842);
  db.Append(first, 91812);
  db.Append(repeat, 91842);

  const protocol::OutPoint prevout{repeat.Transaction(0).GetHash(), 0u};
  protocol::Block spend;
  spend.AddTransaction(MakeCoinbaseLikeTransaction());
  spend.AddTransaction(MakeSpendTransaction(prevout, 1));
  db.Append(spend, 91843);

  const std::vector<OutputKey> keys{prevout};
  std::vector<OutputId> rids(1, kNullOutputId);
  EXPECT_EQ(db.Query(keys, rids, 91844), 0);
  EXPECT_EQ(rids[0], kSpentOutputId);
}

}  // namespace
}  // namespace hornet::data::utxo

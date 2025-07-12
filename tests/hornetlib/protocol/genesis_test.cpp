// #include "hornetlib/protocol/genesis.h"

#include <vector>

#include <gtest/gtest.h>

#include "hornetlib/encoding/reader.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/constants.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/util/hex.h"

namespace hornet::protocol {
namespace {

/*
The genesis block created by Satoshi Nakomoto:

00000000   01 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00   ................
00000010   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00   ................
00000020   00 00 00 00 3B A3 ED FD  7A 7B 12 B2 7A C7 2C 3E   ....;£íýz{.²zÇ,>
00000030   67 76 8F 61 7F C8 1B C3  88 8A 51 32 3A 9F B8 AA   gv.a.È.ÃˆŠQ2:Ÿ¸ª
00000040   4B 1E 5E 4A 29 AB 5F 49  FF FF 00 1D 1D AC 2B 7C   K.^J)«_Iÿÿ...¬+|
00000050   01 01 00 00 00 01 00 00  00 00 00 00 00 00 00 00   ................
00000060   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00   ................
00000070   00 00 00 00 00 00 FF FF  FF FF 4D 04 FF FF 00 1D   ......ÿÿÿÿM.ÿÿ..
00000080   01 04 45 54 68 65 20 54  69 6D 65 73 20 30 33 2F   ..EThe Times 03/
00000090   4A 61 6E 2F 32 30 30 39  20 43 68 61 6E 63 65 6C   Jan/2009 Chancel
000000A0   6C 6F 72 20 6F 6E 20 62  72 69 6E 6B 20 6F 66 20   lor on brink of
000000B0   73 65 63 6F 6E 64 20 62  61 69 6C 6F 75 74 20 66   second bailout f
000000C0   6F 72 20 62 61 6E 6B 73  FF FF FF FF 01 00 F2 05   or banksÿÿÿÿ..ò.
000000D0   2A 01 00 00 00 43 41 04  67 8A FD B0 FE 55 48 27   *....CA.gŠý°þUH'
000000E0   19 67 F1 A6 71 30 B7 10  5C D6 A8 28 E0 39 09 A6   .gñ¦q0·.\Ö¨(à9.¦
000000F0   79 62 E0 EA 1F 61 DE B6  49 F6 BC 3F 4C EF 38 C4   ybàê.aÞ¶Iö¼?Lï8Ä
00000100   F3 55 04 E5 1E C1 12 DE  5C 38 4D F7 BA 0B 8D 57   óU.å.Á.Þ\8M÷º..W
00000110   8A 4C 70 2B 6B F1 1D 5F  AC 00 00 00 00            ŠLp+kñ._¬....
*/

static constexpr uint8_t kGenesisBlockBinary[] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x3B, 0xA3, 0xED, 0xFD, 0x7A, 0x7B, 0x12, 0xB2, 0x7A, 0xC7, 0x2C, 0x3E,
    0x67, 0x76, 0x8F, 0x61, 0x7F, 0xC8, 0x1B, 0xC3, 0x88, 0x8A, 0x51, 0x32, 0x3A, 0x9F, 0xB8, 0xAA,
    0x4B, 0x1E, 0x5E, 0x4A, 0x29, 0xAB, 0x5F, 0x49, 0xFF, 0xFF, 0x00, 0x1D, 0x1D, 0xAC, 0x2B, 0x7C,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x4D, 0x04, 0xFF, 0xFF, 0x00, 0x1D,
    0x01, 0x04, 0x45, 0x54, 0x68, 0x65, 0x20, 0x54, 0x69, 0x6D, 0x65, 0x73, 0x20, 0x30, 0x33, 0x2F,
    0x4A, 0x61, 0x6E, 0x2F, 0x32, 0x30, 0x30, 0x39, 0x20, 0x43, 0x68, 0x61, 0x6E, 0x63, 0x65, 0x6C,
    0x6C, 0x6F, 0x72, 0x20, 0x6F, 0x6E, 0x20, 0x62, 0x72, 0x69, 0x6E, 0x6B, 0x20, 0x6F, 0x66, 0x20,
    0x73, 0x65, 0x63, 0x6F, 0x6E, 0x64, 0x20, 0x62, 0x61, 0x69, 0x6C, 0x6F, 0x75, 0x74, 0x20, 0x66,
    0x6F, 0x72, 0x20, 0x62, 0x61, 0x6E, 0x6B, 0x73, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0xF2, 0x05,
    0x2A, 0x01, 0x00, 0x00, 0x00, 0x43, 0x41, 0x04, 0x67, 0x8A, 0xFD, 0xB0, 0xFE, 0x55, 0x48, 0x27,
    0x19, 0x67, 0xF1, 0xA6, 0x71, 0x30, 0xB7, 0x10, 0x5C, 0xD6, 0xA8, 0x28, 0xE0, 0x39, 0x09, 0xA6,
    0x79, 0x62, 0xE0, 0xEA, 0x1F, 0x61, 0xDE, 0xB6, 0x49, 0xF6, 0xBC, 0x3F, 0x4C, 0xEF, 0x38, 0xC4,
    0xF3, 0x55, 0x04, 0xE5, 0x1E, 0xC1, 0x12, 0xDE, 0x5C, 0x38, 0x4D, 0xF7, 0xBA, 0x0B, 0x8D, 0x57,
    0x8A, 0x4C, 0x70, 0x2B, 0x6B, 0xF1, 0x1D, 0x5F, 0xAC, 0x00, 0x00, 0x00, 0x00};

static constexpr std::span<const uint8_t> kSignatureScript = {&kGenesisBlockBinary[0x7B], 0x4D};

static constexpr std::span<const uint8_t> kPkScript = {&kGenesisBlockBinary[0xD6], 0x43};

bool SpanEqual(std::span<const uint8_t> a, std::span<const uint8_t> b) {
  return a.size() == b.size() && memcmp(a.data(), b.data(), a.size()) == 0;
}

TEST(GenesisTest, DeserializeMatchesFields) {
  const auto& binary = kGenesisBlockBinary;
  encoding::Reader reader{binary};

  Block block;
  block.Deserialize(reader);

  EXPECT_EQ(block.Header().GetVersion(), 1);
  EXPECT_EQ(block.Header().GetPreviousBlockHash(), Hash{});
  EXPECT_EQ(block.Header().GetMerkleRoot(),
            "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash);
  EXPECT_EQ(block.Header().GetTimestamp(), 0x495FAB29);
  EXPECT_EQ(block.Header().GetNonce(), 0x7C2BAC1D);
  EXPECT_EQ(block.Header().GetCompactTarget(), 0x1D00FFFF);
  EXPECT_EQ(block.Header().ComputeHash(), kGenesisHash);

  EXPECT_EQ(block.GetTransactionCount(), 1);

  const auto tx = block.Transaction(0);
  EXPECT_EQ(tx.Version(), 1);
  EXPECT_EQ(tx.InputCount(), 1);
  EXPECT_EQ(tx.Input(0).previous_output.hash, Hash{});
  EXPECT_EQ(tx.Input(0).previous_output.index, 0xFFFFFFFF);
  EXPECT_EQ(tx.SignatureScript(0).size(), 0x4D);
  EXPECT_TRUE(SpanEqual(tx.SignatureScript(0), kSignatureScript));
  EXPECT_EQ(tx.Input(0).sequence, 0xFFFFFFFF);
  EXPECT_EQ(tx.OutputCount(), 1);
  EXPECT_EQ(tx.Output(0).value, 50 * kSatoshisPerCoin);
  EXPECT_EQ(tx.PkScript(0).size(), 0x43);
  EXPECT_TRUE(SpanEqual(tx.PkScript(0), kPkScript));
  EXPECT_EQ(tx.LockTime(), 0);
}

TEST(GenesisTest, SerializationRoundTrip) {
  const std::vector<uint8_t> binary{std::begin(kGenesisBlockBinary), std::end(kGenesisBlockBinary)};
  encoding::Reader reader{binary};
  encoding::Writer writer;

  Block block;
  block.Deserialize(reader);
  block.Serialize(writer);

  const auto& compare = writer.Buffer();
  EXPECT_EQ(compare.size(), binary.size());
  EXPECT_EQ(compare, binary);
}

TEST(GenesisTest, ConstructFromFields) {
  const std::vector<uint8_t> binary{std::begin(kGenesisBlockBinary), std::end(kGenesisBlockBinary)};

  Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.Input(0).previous_output.index = 0xFFFFFFFF;
  tx.Input(0).sequence = 0xFFFFFFFF;
  tx.SetSignatureScript(0, kSignatureScript);
  tx.ResizeOutputs(1);
  tx.Output(0).value = 50 * kSatoshisPerCoin;
  tx.SetPkScript(0, kPkScript);

  BlockHeader header;
  header.SetVersion(1);
  header.SetTimestamp(0x495FAB29);
  header.SetMerkleRoot("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash);
  header.SetCompactTarget(0x1D00FFFF);
  header.SetNonce(0x7C2BAC1D);

  Block block;
  block.SetHeader(header);
  block.AddTransaction(tx);

  encoding::Writer w;
  block.Serialize(w);
  const auto& compare = w.Buffer();

  EXPECT_EQ(compare.size(), binary.size());
  EXPECT_EQ(compare, binary);
}

}  // namespace
}  // namespace hornet::protocol

#include "hornetlib/protocol/transaction.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <vector>

#include "hornetlib/encoding/reader.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/hash.h"

#include <gtest/gtest.h>

namespace hornet::protocol {
namespace {

using Element = std::vector<uint8_t>;
using Stack = std::vector<Element>;

// Serializes a version-2 transaction with one input per stack and a single version-2 witness program output, in the
// BIP144 marker/flag form so that the witness section is present even when every stack is empty.
std::vector<uint8_t> EncodeWithWitnessSection(const std::vector<Stack>& stacks, uint8_t flag = 0x01) {
  encoding::Writer writer;
  writer.WriteLE4(uint32_t{2});
  writer.WriteByte(0x00);  // Marker
  writer.WriteByte(flag);

  writer.WriteVarInt(stacks.size());
  for (size_t i = 0; i < stacks.size(); ++i) {
    Hash prevout_hash{};
    prevout_hash[0] = static_cast<uint8_t>(i + 1);
    writer.WriteBytes(prevout_hash);
    writer.WriteLE4(uint32_t{0});
    writer.WriteVarInt(0);  // Empty scriptSig
    writer.WriteLE4(uint32_t{0xffffffff});
  }

  writer.WriteVarInt(1);
  writer.WriteLE8(uint64_t{42'000});
  std::array<uint8_t, 22> program{0x52, 0x14};  // OP_2 <20 bytes>: a version-2 witness program.
  std::fill(program.begin() + 2, program.end(), 0xab);
  writer.WriteVarInt(program.size());
  writer.WriteBytes(program);

  for (const Stack& stack : stacks) {
    writer.WriteVarInt(stack.size());
    for (const Element& element : stack) {
      writer.WriteVarInt(element.size());
      writer.WriteBytes(element);
    }
  }

  writer.WriteLE4(uint32_t{0});
  return writer.Buffer();
}

// Core rejects a witness section only when every stack is empty (CScriptWitness::IsNull). A stack holding one empty
// element is a non-null witness, and is consensus-valid when spending a witness program of unknown version.
TEST(TransactionTest, DeserializesWitnessStackOfOneEmptyElement) {
  const auto bytes = EncodeWithWitnessSection({Stack(1)});

  Transaction tx;
  encoding::Reader reader{bytes};
  ASSERT_NO_THROW(tx.Deserialize(reader));
  EXPECT_EQ(reader.GetPos(), bytes.size());

  EXPECT_TRUE(tx.IsWitness());
  ASSERT_EQ(tx.WitnessCount(), 1);
  ASSERT_EQ(tx.ComponentCount(0), 1);
  EXPECT_TRUE(tx.WitnessScript(0, 0).empty());

  // Marker, flag, stack count and element length are the only witness bytes.
  EXPECT_EQ(tx.SerializedBytesNoWitness(), std::ssize(bytes) - 4);

  encoding::Writer writer;
  tx.Serialize(writer);
  EXPECT_EQ(writer.Buffer(), bytes);
}

TEST(TransactionTest, DeserializesWitnessWhenOnlyOneStackHoldsAnEmptyElement) {
  const auto bytes = EncodeWithWitnessSection({Stack{}, Stack(1)});

  Transaction tx;
  encoding::Reader reader{bytes};
  ASSERT_NO_THROW(tx.Deserialize(reader));
  EXPECT_EQ(reader.GetPos(), bytes.size());

  ASSERT_EQ(tx.WitnessCount(), 2);
  EXPECT_EQ(tx.ComponentCount(0), 0);
  EXPECT_EQ(tx.ComponentCount(1), 1);
}

// Core: "Superfluous witness record".
TEST(TransactionTest, RejectsWitnessSectionWhenEveryStackIsEmpty) {
  const auto bytes = EncodeWithWitnessSection({Stack{}, Stack{}});

  Transaction tx;
  encoding::Reader reader{bytes};
  EXPECT_THROW(tx.Deserialize(reader), std::exception);
}

// Core: "Unknown transaction optional data".
TEST(TransactionTest, RejectsWitnessFlagOtherThanOne) {
  const auto bytes = EncodeWithWitnessSection({Stack{Element{0x01}}}, 0x02);

  Transaction tx;
  encoding::Reader reader{bytes};
  EXPECT_THROW(tx.Deserialize(reader), std::exception);
}

// Core reads a flag byte of zero as "no optional data" and then finds no inputs, so it parses an empty transaction
// and rejects the block under its empty-inputs rule. Hornet rejects the encoding itself. The block is invalid either
// way.
TEST(TransactionTest, RejectsZeroWitnessFlag) {
  const auto bytes = EncodeWithWitnessSection({Stack{Element{0x01}}}, 0x00);

  Transaction tx;
  encoding::Reader reader{bytes};
  EXPECT_THROW(tx.Deserialize(reader), std::exception);
}

// Core's block handler deserializes the block and ignores whatever follows it in the message, so trailing bytes are
// neither an error nor part of the block's size or weight.
TEST(TransactionTest, BlockDeserializationIgnoresTrailingBytes) {
  const auto tx_bytes = EncodeWithWitnessSection({Stack{Element{0x01}}});

  encoding::Writer writer;
  BlockHeader{}.Serialize(writer);
  writer.WriteVarInt(1);
  writer.WriteBytes(tx_bytes);
  const int block_size = std::ssize(writer.Buffer());

  const std::array<uint8_t, 5> trailing = {0xde, 0xad, 0xbe, 0xef, 0x00};
  writer.WriteBytes(trailing);
  const auto& bytes = writer.Buffer();

  Block block;
  encoding::Reader reader{bytes};
  ASSERT_NO_THROW(block.Deserialize(reader));
  EXPECT_EQ(reader.GetPos(), static_cast<size_t>(block_size));

  constexpr int kWitnessBytes = 5;  // Marker, flag, stack count, element length, element.
  EXPECT_EQ(block.GetStrippedSize(), block_size - kWitnessBytes);
  EXPECT_EQ(block.GetWeightUnits(), 4 * block_size - 3 * kWitnessBytes);
}

TEST(TransactionTest, BlockWeightCountsEmptyElementWitnessBytes) {
  const auto tx_bytes = EncodeWithWitnessSection({Stack(1)});

  encoding::Writer writer;
  BlockHeader{}.Serialize(writer);
  writer.WriteVarInt(1);
  writer.WriteBytes(tx_bytes);
  const auto& bytes = writer.Buffer();

  Block block;
  encoding::Reader reader{bytes};
  ASSERT_NO_THROW(block.Deserialize(reader));

  constexpr int kWitnessBytes = 4;  // Marker, flag, stack count, element length.
  const int total = std::ssize(bytes);
  EXPECT_EQ(block.GetStrippedSize(), total - kWitnessBytes);
  EXPECT_EQ(block.GetWeightUnits(), 4 * total - 3 * kWitnessBytes);
}

}  // namespace
}  // namespace hornet::protocol

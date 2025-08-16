// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/protocol/block.h"

#include <array>

#include "hornetlib/encoding/reader.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/protocol/constants.h"
#include "hornetlib/protocol/transaction.h"

#include <gtest/gtest.h>

namespace hornet::protocol {

TEST(BlockTest, GetGenesis) {
  const auto& genesis = Block::Genesis();
  EXPECT_EQ(genesis.Header().ComputeHash(), kGenesisHash);
}

TEST(BlockTest, TestGetWeightUnits) {
  protocol::Block block;

  // Manually construct a tx with known serialization size and witness size
  protocol::Transaction tx;
  tx.SetVersion(2);
  tx.ResizeInputs(1);
  tx.Input(0).previous_output.hash = protocol::Hash{0x01};
  tx.Input(0).previous_output.index = 0;
  tx.Input(0).sequence = 0xffffffff;
  tx.ResizeOutputs(1);
  tx.Output(0).value = 42'000;
  const std::array<uint8_t, 3> script = {0xaa, 0xbb, 0xcc};
  tx.SetPkScript(0, std::span(script));
  tx.ResizeWitnesses(1);
  tx.ResizeComponents(0, 1);
  const std::array<uint8_t, 2> wscript = {0x11, 0x22};
  tx.SetWitnessScript(0, 0, std::span(wscript));
  tx.SetLockTime(0);

  block.AddTransaction(tx);

  // Serialize the block to buffer
  encoding::Writer writer;
  block.Serialize(writer);
  const auto& buffer = writer.Buffer();

  // Now weight units should be accurate
  const int total_bytes = std::ssize(buffer);

  // Serialize the block without witness data
  encoding::Writer writer2;
  block.Serialize(writer2, false);
  const int witness_bytes = total_bytes - std::ssize(writer2.Buffer());
  
  const int expected_weight = 4 * total_bytes - 3 * witness_bytes;

  // Deserialize into a new block to set the sizes correctly.
  protocol::Block deserialized;
  encoding::Reader reader(buffer);
  deserialized.Deserialize(reader);

  EXPECT_EQ(deserialized.GetWeightUnits(), expected_weight);
}

}  // namespacae hornet::protocol

#include "hornetlib/data/utxo/block_outputs.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "hornetlib/data/utxo/codec.h"
#include "hornetlib/data/utxo/types.h"
#include "hornetlib/util/assert.h"

namespace hornet::data::utxo {
namespace {

TEST(BlockOutputsTest, BasicProperties) {
  std::vector<uint8_t> data = {10, 20, 30};
  BlockOutputs blk(100, 42, std::move(data));

  EXPECT_EQ(blk.BeginOffset(), 100u);
  EXPECT_EQ(blk.EndOffset(), 103u);  // offset + 3 bytes
  EXPECT_EQ(blk.Length(), 3);
  EXPECT_EQ(blk.Height(), 42);

  auto span = blk.Data();
  ASSERT_EQ(span.size(), 3u);
  EXPECT_EQ(span[0], 10);
  EXPECT_EQ(span[1], 20);
  EXPECT_EQ(span[2], 30);
}

TEST(BlockOutputsTest, FetchDataSingleRid) {
  std::vector<uint8_t> data = {1, 2, 3, 4, 5};
  BlockOutputs blk(10, 0, std::move(data));

  OutputId rid = IdCodec::Encode(12, 2);  // offset=12 → relative 2 (since base=10), length=2
  OutputDetail detail = { OutputHeader::Null(), {} };
  uint8_t buffer[10] = {};
  const int count = blk.FetchData({&rid, 1}, {&detail, 1}, buffer, sizeof(buffer));

  EXPECT_EQ(count, 1);
  EXPECT_EQ(buffer[0], 3);
  EXPECT_EQ(buffer[1], 4);
}

TEST(BlockOutputsTest, FetchDataMultipleRids) {
  std::vector<uint8_t> data = {10, 11, 12, 13, 14, 15};
  BlockOutputs blk(100, 7, std::move(data));

  OutputId rids[2] = {
      IdCodec::Encode(100, 3),  // offset=100 → relative 0, len=3
      IdCodec::Encode(103, 2)   // offset=103 → relative 3, len=2
  };
  std::vector<OutputDetail> detail(2, { OutputHeader::Null(), {} });
  uint8_t buffer[10] = {};
  const int count = blk.FetchData(rids, detail, buffer, sizeof(buffer));

  EXPECT_EQ(count, 2);
  EXPECT_EQ(buffer[0], 10);
  EXPECT_EQ(buffer[1], 11);
  EXPECT_EQ(buffer[2], 12);
  EXPECT_EQ(buffer[3], 13);
  EXPECT_EQ(buffer[4], 14);
}

TEST(BlockOutputsTest, MoveConstructorTransfersOwnership) {
  std::vector<uint8_t> data = {8, 9};
  BlockOutputs original(99, 5, std::move(data));
  BlockOutputs moved(std::move(original));

  EXPECT_EQ(moved.BeginOffset(), 99u);
  EXPECT_EQ(moved.Height(), 5);
  EXPECT_EQ(moved.Length(), 2);
  EXPECT_EQ(moved.Data()[0], 8);
  EXPECT_EQ(moved.Data()[1], 9);
}

}  // namespace
}  // namespace hornet::data::utxo

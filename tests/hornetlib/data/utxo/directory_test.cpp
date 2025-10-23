#include "hornetlib/data/utxo/directory.h"

#include <tuple>

#include <gtest/gtest.h>

#include "hornetlib/data/utxo/tiled_vector.h"
#include "hornetlib/data/utxo/types.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/util/hex.h"

namespace hornet::data::utxo {
namespace {

inline OutputKV Create(const protocol::Hash& hash) {
  return { { hash, 0 }, { 0, OutputKV::Add }, kNullOutputId };
}

TEST(DirectoryTest, TestDirectory) {
  constexpr protocol::Hash hash = "0000000000000000000000000000000000000000000000000000000000000063"_hash;

  TiledVector<OutputKV> tv;
  tv.PushBack(Create(hash));

  Directory directory(8, tv.begin(), tv.end());
  
  EXPECT_EQ(directory.Size(), 256 + 1);
  for (int i = 0; i < directory.Size(); ++i)
    EXPECT_EQ(directory[i], i <= 0x63 ? 0 : 1);

  EXPECT_EQ(tv[directory[0x63]].key.hash, hash);
  EXPECT_EQ(directory.LookupRange(tv[0].key), std::make_pair(0, 1));
}

}  // namespace
}  // namespace hornet::data::utxo

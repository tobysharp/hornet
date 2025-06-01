#include "protocol/block_header.h"

#include "crypto/hash.h"

#include <gtest/gtest.h>

namespace hornet::protocol {
namespace {

TEST(BlockHeader, GenesisBlockHashMatches) {
  // Genesis block header fields
  BlockHeader header;
  header.SetVersion(1);
  header.SetPreviousBlockHash(Hash{});  // all zeros
  header.SetMerkleRoot(crypto::ParseHex32ToLE("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));
  header.SetTimestamp(1231006505);
  header.SetBits(0x1d00ffff);
  header.SetNonce(2083236893);
  EXPECT_EQ(header.GetHash(), kGenesisHash);
  EXPECT_FALSE(!header.IsProofOfWork());
}

}  // namespace
}  // namespace hornet::protocol
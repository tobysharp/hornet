#include "hornetlib/consensus/merkle.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/transaction.h"

#include "testutil/blockchain.h"

#include "hornetlib/consensus/validate_chain_harness.h"

#include <gtest/gtest.h>

namespace hornet {
namespace {

void FixMerkleRoot(protocol::Block& block) {
  auto header = block.Header();
  header.SetMerkleRoot(consensus::ComputeMerkleRoot(block).hash);
  block.SetHeader(header);
}

// Duplicating an unspent coinbase transaction early in the chain violates the BIP30 check.
TEST(ValidateSpendingTest, ProcessDuplicateOutPoint) {
  test::ExpectValidationResult([] {
    // A few empty blocks...
    test::Blockchain data;
    for (int height = 1; height < 4; ++height)
      data.Append(data.Sample(1'000, true));

    // ... followed by a block that duplicates an earlier coinbase ...
    data[3]->Transaction(0).CopyFrom(data[1]->Transaction(0));  

    // ... and just patch up the Merkle root.
    FixMerkleRoot(*data[3]);
    return data;
  }, consensus::Error::Spending_DuplicateOutPoint);
}

// Duplicating a fully-spent coinbase transaction does not violate the BIP30 check.
TEST(ValidateSpendingTest, ProcessFullySpentDuplicateOutPoint) {
  test::ExpectValidationResult([] {
    test::Blockchain data;
    
    // We load a pre-mined chain to bypass the 100-block coinbase maturity waiting.
    // The chain is long enough that Block 1's coinbase has matured and been fully spent.
    data.Load(test::GetDataPath("ValidationPipelineTest_ProcessBlocks.bin"));

    // Append block: Duplicate the now fully-spent coinbase of Block 1
    data.Append(data.Sample(1'000, true));
    data.Back()->Transaction(0).CopyFrom(data[1]->Transaction(0));
    FixMerkleRoot(*data.Back());
    return data;
  });
}

TEST(ValidateSpendingTest, ProcessOutputAmountsExceedInputAmounts) {
  test::ExpectValidationResult([] {
    test::Blockchain data;

    // We load a pre-mined chain so coinbase outputs are mature and spendable.
    data.Load(test::GetDataPath("ValidationPipelineTest_ProcessBlocks.bin"));

    // Append a valid spending block, then corrupt one spend so its outputs exceed its inputs.
    data.Append(data.Sample(2, true));
    data.Back()->Transaction(1).Output(0).value += 1;
    FixMerkleRoot(*data.Back());
    return data;
  }, consensus::Error::Spending_OutputAmountsExceedInputAmounts);
}

}  // namespace
}  // namespace hornet

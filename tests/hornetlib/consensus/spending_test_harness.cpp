#include "hornetlib/consensus/spending_test_harness.h"

#include "hornetlib/consensus/merkle.h"

#include "testutil/data_path.h"

namespace hornet::test {

void FixMerkleRoot(protocol::Block& block) {
  auto header = block.Header();
  header.SetMerkleRoot(consensus::ComputeMerkleRoot(block).hash);
  block.SetHeader(header);
}

Blockchain LoadValidationPipelineChain() {
  Blockchain chain;
  chain.Load(test::GetDataPath("ValidationPipelineTest_ProcessBlocks.bin"));
  return chain;
}

}  // namespace hornet::test
<style>
body.markdown-body {
  font-family: "Iowan Old Style", "Palatino Linotype", "Book Antiqua", "URW Palladio L", Georgia, serif;
  text-align: left;
  max-width: 1200px;
  width: min(1200px, calc(100vw - 48px));
  margin-left: auto;
  margin-right: auto;
  padding-left: 24px;
  padding-right: 24px;  
}

body.markdown-body h1,
body.markdown-body h2,
body.markdown-body h3,
body.markdown-body h4 {
  font-family: "Avenir Next", "Segoe UI", "Helvetica Neue", Arial, sans-serif;
}

body.markdown-body code,
body.markdown-body pre {
  font-family: "JetBrains Mono", "Fira Code", Consolas, monospace;
}
</style>

# Hornet: Bitcoin Consensus Specification
v0.1. Height 941,828 \
Copyright © 2026 Toby Sharp \
*toby@hornetnode.org*

Below is a complete list of all the semantic invariants that must be satisfied for a block to be consensus-valid. This table has been auto-generated from the Hornet executable declarative [specification](../src/hornetlib/consensus/rules/spec.h) source code.

## Block Validation Rules

| ID | Rule | Function |
|-|-|-|
|
||**Header Rules**
|
H01|A header MUST reference the hash of a valid parent block.|[`ValidatePreviousHash`](../src/hornetlib/consensus/rules/validate_header.h#L33)
H02|A header's hash MUST achieve its own proof-of-work target.|[`ValidateProofOfWork`](../src/hornetlib/consensus/rules/validate_header.h#L39)
H03|A header's proof-of-work target MUST satisfy the difficulty adjustment formula for the timechain.|[`ValidateDifficultyAdjustment`](../src/hornetlib/consensus/rules/validate_header.h#L47)
H04|A header timestamp MUST be strictly greater than the median of its 11 ancestors' timestamps.|[`ValidateMedianTimePast`](../src/hornetlib/consensus/rules/validate_header.h#L54)
H05|A header timestamp MUST be less than or equal to network-adjusted time plus 2 hours.|[`ValidateTimestampCurrent`](../src/hornetlib/consensus/rules/validate_header.h#L60)
H06|A header's version number MUST NOT have been retired by any activated soft fork.|[`ValidateVersion`](../src/hornetlib/consensus/rules/validate_header.h#L68)
|
||**Local Rules**
|
L01|A block MUST contain at least one transaction.|[`ValidateNonEmpty`](../src/hornetlib/consensus/rules/validate_local.h#L30)
L02|A block’s Merkle root field MUST equal the unique Merkle root of its transactions.|[`ValidateMerkleRoot`](../src/hornetlib/consensus/rules/validate_local.h#L36)
L03|A block’s serialized size excluding witness flags and data MUST NOT exceed 1,000,000 bytes.|[`ValidateOriginalSizeLimit`](../src/hornetlib/consensus/rules/validate_local.h#L44)
L04|A block's first transaction MUST be its only coinbase transaction.|[`ValidateCoinbase`](../src/hornetlib/consensus/rules/validate_local.h#L51)
L05|A transaction MUST contain at least one input.|[`ValidateInputCount`](../src/hornetlib/consensus/rules/validate_transaction.h#L13)
L06|A transaction MUST contain at least one output.|[`ValidateOutputCount`](../src/hornetlib/consensus/rules/validate_transaction.h#L20)
L07|A transaction's serialized size excluding witness flags and data MUST NOT exceed 1,000,000 bytes.|[`ValidateTransactionSize`](../src/hornetlib/consensus/rules/validate_transaction.h#L27)
L08|All transaction output amounts MUST be non-negative.|[`ValidateOutputsNonNegative`](../src/hornetlib/consensus/rules/validate_transaction.h#L35)
L09|The sum of a transaction's output amounts MUST NOT exceed 21,000,000 coins.|[`ValidateOutputsSum`](../src/hornetlib/consensus/rules/validate_transaction.h#L44)
L10|A transaction's inputs MUST NOT contain duplicate outpoints.|[`ValidateUniqueInputs`](../src/hornetlib/consensus/rules/validate_transaction.h#L59)
L11|A coinbase transaction's sig script size MUST be between 2 and 100 bytes inclusive.|[`ValidateCoinbaseSignatureSize`](../src/hornetlib/consensus/rules/validate_transaction.h#L73)
L12|A non-coinbase transaction's inputs MUST have non-null previous outputs.|[`ValidateInputsPrevout`](../src/hornetlib/consensus/rules/validate_transaction.h#L85)
L13|The total legacy signature operation count over all input and output scripts MUST NOT exceed 20,000.|[`ValidateSignatureOps`](../src/hornetlib/consensus/rules/validate_local.h#L58)
|
||**Contextual Rules**
|
C01|All transactions in the block MUST be final given the block height and locktime rules.|[`ValidateTransactionFinality`](../src/hornetlib/consensus/rules/validate_contextual.h#L54)
C02|From BIP34: The coinbase transaction’s sig script MUST begin by pushing the block height.|[`ValidateCoinbaseHeight`](../src/hornetlib/consensus/rules/validate_contextual.h#L66)
C03|From BIP141: A block containing witness data MUST contain a witness commitment.|[`ValidateWitnessDataHasCommitment`](../src/hornetlib/consensus/rules/validate_contextual.h#L74)
C04|From BIP141: A post-Segwit block containing a witness commitment MUST contain a witness nonce.|[`ValidateWitnessNonce`](../src/hornetlib/consensus/rules/validate_contextual.h#L85)
C05|From BIP141: A post-SegWit block containing a witness commitment MUST commit to its witness Merkle root and nonce.|[`ValidateWitnessMerkle`](../src/hornetlib/consensus/rules/validate_contextual.h#L92)
C06|A pre-SegWit block MUST NOT contain any witness data.|[`ValidateNoWitnessPreSegwit`](../src/hornetlib/consensus/rules/validate_contextual.h#L109)
C07|A block’s total weight MUST NOT exceed 4,000,000 weight units.|[`ValidateBlockWeight`](../src/hornetlib/consensus/rules/validate_contextual.h#L119)
|
||**Spending Rules**
|
S01|Transaction outputs MUST NOT give rise to duplicates of existing unspent outpoints (BIP30).|[`ValidateOutPointsUnique`](../src/hornetlib/consensus/rules/validate_spending.h#L114)
S02|A transaction input MUST reference a previous transaction output that remains unspent.|[`ValidateInputPrevoutsUnspent`](../src/hornetlib/consensus/rules/validate_spending.h#L109)
S03|Coinbase outputs MUST NOT be spent before 100 blocks after their creation.|[`ValidateCoinbaseMaturity`](../src/hornetlib/consensus/rules/validate_spending.h#L24)
S04|The sum of output values in a transaction MUST NOT exceed the sum of all input values being spent.|[`ValidateOutputsAtMostInputs`](../src/hornetlib/consensus/rules/validate_spending.h#L38)
S05|From BIP68: Each input that signals a relative lock-time interval MUST have reached relative finality.|[`ValidateSequenceLocks`](../src/hornetlib/consensus/rules/validate_spending.h#L47)
S06|A non-coinbase input's sig script and spent output’s pubkey script MUST evaluate successfully.|[`ValidateScripts`](../src/hornetlib/consensus/rules/validate_spending.h#L89)
S07|The sum of sigop costs over all transactions MUST NOT exceed 80,000.|[`ValidateSigOpCosts`](../src/hornetlib/consensus/rules/validate_spending.h#L141)
S08|The total amount in coinbase outputs MUST NOT exceed the block reward.|[`ValidateBlockSubsidy`](../src/hornetlib/consensus/rules/validate_spending.h#L155)

## Definitions

| Term | Definition |
|-|-|
Block|A *block* comprises a header and an ordered sequence of transactions.
Genesis|The *genesis* block is the root node of the Bitcoin timechain.
Header|A *header* is a structured set of fields over the first 80 bytes in a block.
Timechain|A *timechain* is a tree of validated blocks.

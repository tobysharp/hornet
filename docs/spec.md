<style>
body.markdown-body {
  font-family: "Iowan Old Style", "Palatino Linotype", "Book Antiqua", "URW Palladio L", Georgia, serif;
  font-size: 13pt;
  text-align: left;
  max-width: 1100px;
  width: min(1100px, calc(100vw - 48px));
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
Copyright © 2026 Toby Sharp \
*toby@hornetnode.org*

Below is an auto-generated rendering of Hornet’s [declarative specification](../src/hornetlib/consensus/rules/spec.h) of Bitcoin block validity. It lists the semantic invariants that together determine whether a block is consensus-valid, with links to the corresponding Hornet validation functions that realize those invariants in executable form.

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
L05|The total legacy signature-operation count over all input and output scripts MUST NOT exceed 20,000.|[`ValidateSignatureOps`](../src/hornetlib/consensus/rules/validate_local.h#L58)
L06|A transaction MUST contain at least one input.|[`ValidateInputCount`](../src/hornetlib/consensus/rules/validate_transaction.h#L13)
L07|A transaction MUST contain at least one output.|[`ValidateOutputCount`](../src/hornetlib/consensus/rules/validate_transaction.h#L20)
L08|A transaction's serialized size excluding witness flags and data MUST NOT exceed 1,000,000 bytes.|[`ValidateTransactionSize`](../src/hornetlib/consensus/rules/validate_transaction.h#L27)
L09|All transaction output amounts MUST be non-negative.|[`ValidateOutputsNonNegative`](../src/hornetlib/consensus/rules/validate_transaction.h#L35)
L10|The sum of a transaction's output amounts MUST NOT exceed 21,000,000 coins.|[`ValidateOutputsSum`](../src/hornetlib/consensus/rules/validate_transaction.h#L44)
L11|A transaction's inputs MUST NOT contain duplicate outpoints.|[`ValidateUniqueInputs`](../src/hornetlib/consensus/rules/validate_transaction.h#L59)
L12|A coinbase's sig script size MUST be between 2 and 100 bytes inclusive.|[`ValidateCoinbaseSignatureSize`](../src/hornetlib/consensus/rules/validate_transaction.h#L73)
L13|A non-coinbase transaction's inputs MUST have non-null previous outputs.|[`ValidateInputsPrevout`](../src/hornetlib/consensus/rules/validate_transaction.h#L85)
|
||**Contextual Rules**
|
C01|All transactions in the block MUST be final given the block height and locktime rules.|[`ValidateTransactionFinality`](../src/hornetlib/consensus/rules/validate_contextual.h#L54)
C02|A pre-SegWit block MUST NOT contain any witness data.|[`ValidateNoWitnessPreSegwit`](../src/hornetlib/consensus/rules/validate_contextual.h#L109)
C03|A block’s total weight MUST NOT exceed 4,000,000 weight units.|[`ValidateBlockWeight`](../src/hornetlib/consensus/rules/validate_contextual.h#L119)
C04|From BIP34: A coinbase's sig script MUST begin by pushing the block height.|[`ValidateCoinbaseHeight`](../src/hornetlib/consensus/rules/validate_contextual.h#L66)
C05|From BIP141: A block containing witness data MUST contain a witness commitment.|[`ValidateWitnessCommitment`](../src/hornetlib/consensus/rules/validate_contextual.h#L74)
C06|From BIP141: A post-Segwit block containing a witness commitment MUST contain a witness nonce.|[`ValidateWitnessNonce`](../src/hornetlib/consensus/rules/validate_contextual.h#L85)
C07|From BIP141: A post-SegWit block containing a witness commitment MUST commit to its witness Merkle root and nonce.|[`ValidateWitnessMerkle`](../src/hornetlib/consensus/rules/validate_contextual.h#L92)
|
||**Spending Rules**
|
S01|BIP30: Transaction outputs MUST NOT give rise to outpoints that reference existing unspent outputs, except in blocks listed in BIP30 Exceptions.|[`ValidateOutPointsUnique`](../src/hornetlib/consensus/rules/validate_spending.h#L119)
S02|A non-coinbase input MUST reference an output created in a preceding transaction.|[`ValidateInputPrevoutsCreated`](../src/hornetlib/consensus/rules/validate_spending.h#L108)
S03|A non-coinbase input MUST NOT reference an output that was spent in a preceding transaction.|[`ValidateInputPrevoutsUnspent`](../src/hornetlib/consensus/rules/validate_spending.h#L113)
S04|The total signature-operation cost over all transactions MUST NOT exceed 80,000.|[`ValidateSigOpCosts`](../src/hornetlib/consensus/rules/validate_spending.h#L145)
S05|The total amount in coinbase outputs MUST NOT exceed the block reward.|[`ValidateBlockSubsidy`](../src/hornetlib/consensus/rules/validate_spending.h#L159)
S06|The sum of output values in a transaction MUST NOT exceed the sum of all input values being spent.|[`ValidateOutputsAtMostInputs`](../src/hornetlib/consensus/rules/validate_spending.h#L38)
S07|A non-coinbase input MUST satisfy the spent output's locking script.|[`ValidateScripts`](../src/hornetlib/consensus/rules/validate_spending.h#L88)
S08|From BIP68: Each input that signals a relative lock-time interval MUST have reached relative finality.|[`ValidateSequenceLocks`](../src/hornetlib/consensus/rules/validate_spending.h#L46)
S09|Coinbase outputs MUST NOT be spent before 100 blocks after their creation.|[`ValidateCoinbaseMaturity`](../src/hornetlib/consensus/rules/validate_spending.h#L24)

## Definitions

| Term | Definition |
|-|-|
Block|A *block* comprises a header and an ordered sequence of transactions.
Genesis|The *genesis* block is the first block in the Bitcoin timechain.
Header|A *header* is a structured set of fields over the first 80 bytes in a block.
Timechain|A *timechain* is a tree of validated blocks.
Proof of Work|A 256-bit hash must be found that does not exceed a given target value. 
Preceding Transaction|A preceding transaction is one that appears in an ancestor block or earlier in the current block.
Outpoint|An outpoint contains a transaction hash and output index. In a transaction input, it references the output of the most recent preceding transaction with the specified hash and index.
Spent|An output is spent when it is referenced by a transaction input.
Coinbase|A coinbase is a transaction with exactly one input whose outpoint is null.
Witness|Witness data is segregated transaction input data committed separately under SegWit rules.
Signature Operation|A signature operation is a script accounting unit associated with signature-checking opcodes.
BIP34|The coinbase height soft fork is active from block height 227,931.
BIP68|The sequence locks soft fork is active from block height 419,328.
BIP141|The SegWit soft fork is active from block height 481,824.
## BIP30 Exceptions

| Block Height | Block Hash |
|-|-|
91842|00000000000a4d0a398161ffc163c503763b1f4360639393e0e4c8e300e0caec
91880|00000000000743f190a18c5577a3c2d2a1f610ae9601ac046a38084ccb7cd721

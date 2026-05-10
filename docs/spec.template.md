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
@graph(kConsensusRules)

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
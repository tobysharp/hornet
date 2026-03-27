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
@graph(kConsensusRules)

## Definitions

| Term | Definition |
|-|-|
Block|A *block* comprises a header and an ordered sequence of transactions.
Genesis|The *genesis* block is the root node of the Bitcoin timechain.
Header|A *header* is a structured set of fields over the first 80 bytes in a block.
Timechain|A *timechain* is a tree of validated blocks.

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

## Definitions

| Term | Definition |
|-|-|
Header|A *header* is a structured set of fields over the first 80 bytes in a block.

## Block Validation Rules

| ID | Rule | Function |
|-|-|-|
@graph(kBlockRules)

## Script Execution
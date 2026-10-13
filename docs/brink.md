---
marp: true
theme: hornet
paginate: false
---

<!-- _class: title -->

<img class="banner" src="banner.png" />

<!-- These are my notes. 
Second line.

Second paragraph.-->

<!-- Another comment. -->

<div class="content">
<h1>A Minimal, Executable Specification<br/>for Bitcoin Consensus</h1>
<div class="author">Toby Sharp</div>
<div class="event">Brink Engineering Call · 12 June 2026</div>
</div>


<!-- Another comment. -->

---

# Every major protocol has a technical specification.

| Category | Protocols |
|---|---|
| **Network** | HTTP, TCP/IP, TLS, DNS, SMTP |
| **Wireless** | Wi-Fi, Bluetooth, NFC |
| **Web** | HTML, CSS, JavaScript |
| **Languages** | C, C++, Java |
| **Media** | JPEG, MPEG, HDTV, CD, DVD, BluRay |
| **Data** | PDF, XML, JSON, Unicode |
| **Hardware** | USB, HDMI, PCIe, Thunderbolt |
| **Cryptography** | AES, RSA, SHA, ECDSA |

**Bitcoin** |  `bitcoin/src/*` ?

---

# The brittleness problem

<div class="bar-item">
<div class="bar-title">Code is the spec</div>
<div class="bar-body">Consensus rules live inside one codebase, entangled with storage, threading, and state.</div>
</div>

<div class="bar-item">
<div class="bar-title">Refactoring is risky</div>
<div class="bar-body">Any deep change might alter implicit behaviour. Reviewers must verify nothing shifted — by hand.</div>
</div>

<div class="bar-item">
<div class="bar-title">Software ossifies</div>
<div class="bar-body">The protocol should ossify. The software shouldn't. But when they're fused, both freeze together.</div>
</div>

<div class="bar-item">
<div class="bar-title">Client monoculture</div>
<div class="bar-body">Without a specification, the only safe implementation is the reference codebase. Client diversity becomes a consensus risk rather than a resilience feature.</div>
</div>

---

# What is Hornet?

**A specification of Bitcoin consensus**
Including 35 semantic rules defining block validity, in plain language and executable code.

**A consensus validation library**
Evaluating whether a block meets the consensus specification.

**An experimental IBD client**
Able to sync and validate mainnet to tip.

---

# Two approaches to sharing consensus

## Both share the consensus rules. The question is what comes attached.

<div class="two-col">
<div class="col">
<h3 class="white">libbitcoinkernel</h3>
<p class="col-desc">Shares an implementation</p>
<p class="white"><i>"How do we let other clients share Core's consensus code?"</i></p>
<p>The rules are welded to implementation choices: state model, memory layout, disk format, API, sequential processing.</p>
<p>You inherit the whole stack with its limitations to get the rules.</p>
</div>
<div class="col">
<h3 class="orange">Hornet</h3>
<p class="col-desc">Shares a specification</p>
<p class="white"><i>"What does consensus look like written from scratch, based on a specification?"</i></p>
<p>The pure semantic logic of consensus, with no implementation attached.</p>
<p>Shared semantics, independent implementations — written by hand or generated from the spec.</p>
</div>
</div>

---

# Properties of a specification

<div class="bar-item">
<div class="bar-title">Semantic</div>
<div class="bar-body">Meaningful rules and definitions. One logical invariant per rule, with a plain-language reading.</div>
</div>

<div class="bar-item">
<div class="bar-title">Declarative</div>
<div class="bar-body">What must be true to stay in consensus, not how to compute it.</div>
</div>

<div class="bar-item">
<div class="bar-title">Pure</div>
<div class="bar-body">No side effects, mutable state, or dependency on memory/threading/data model. Implementation-neutral by construction.</div>
</div>

<div class="bar-item">
<div class="bar-title">Executable</div>
<div class="bar-body">Compiles, runs, validates mainnet to tip. Testable against Core on historical and putative blocks.</div>
</div>

---

# Hornet's roadmap
 
<div class="step step-active"><div class="step-num">1</div><div class="step-text"><div class="step-title">English semantic spec <span class="badge badge-done">DONE</span></div><div class="step-note">The readable view: 35 named rules for block validation</div></div></div>
<div class="step step-active"><div class="step-num">2</div><div class="step-text"><div class="step-title">Declarative C++ spec <span class="badge badge-done">DONE</span></div><div class="step-note">The executable view: the same rules, validating mainnet to tip</div></div></div>
<div class="step step-future"><div class="step-num">3</div><div class="step-text"><div class="step-title">Differential testing at scale <span class="badge badge-wip">NEXT</span></div><div class="step-note"><span class="orange">Empirical</span> — reject paths and constructed invalid blocks, across implementations</div></div></div>
<div class="step step-active"><div class="step-num">4</div><div class="step-text"><div class="step-title">Pure functional DSL <span class="badge badge-wip">IN PROGRESS</span></div><div class="step-note">One artifact, both readable and executable — no correspondence to maintain</div></div></div>
<div class="step step-future"><div class="step-num">5</div><div class="step-text"><div class="step-title">DSL-driven implementations</div><div class="step-note"><span class="orange">Constructive</span> — consensus logic generated from one spec, correct by construction</div></div></div>
<div class="step step-future"><div class="step-num">6</div><div class="step-text"><div class="step-title">Formal verification against the spec</div><div class="step-note"><span class="orange">Deductive</span> — a machine-checked proof across all inputs</div></div></div>


---

# The Readable View of Specification

---

# Block Validation Rules

## Semantic: <span class="grey">Each rule is a <span class="white"><i>single meaningful invariant</i></span> property.</span> 
## Declarative: <span class="grey">Rules say <span class="white"><i>what must be true</i></span>, not how to evaluate them.</span>
<br/>

35 rules currently split into:

- **Header Rules** (6): Depend only on headers.
- **Local Rules** (13): Depend only on the current block.
- **Contextual Rules** (7): Depend on a block's position within the chain.
- **Spending Rules** (9): Depend on the previous outputs being spent.

<br/>

Full specification at https://hornetnode.org/spec.html.

<div class="footnote">Rule grouping may change</div>



---

# 1. Header Rules
 
<br>

**H01.** A header MUST reference the hash of a valid parent block.

**H02.** A header's hash MUST NOT exceed its own proof-of-work target.

**H03.** A header's proof-of-work target MUST satisfy the difficulty adjustment formula for the timechain.

**H04.** A header timestamp MUST be greater than the median of its 11 ancestor blocks' timestamps.

**H05.** A header timestamp MUST NOT exceed network-adjusted time plus 2 hours.

**H06.** A header's version number MUST NOT have been retired by any activated soft fork. (See Table 1.)

---

# 2. Local Rules

**L01.** A block MUST contain at least one transaction.
**L02.** A block’s Merkle root field MUST equal the unique Merkle root of its transactions.
**L03.** A block’s serialized size excluding witness flags and data MUST NOT exceed 1,000,000 bytes.
**L04.** A block's first transaction MUST be its only coinbase transaction.
**L05.** <span class="small">A block's legacy signature-operation count over all input and output scripts MUST NOT exceed 20,000.</span>
**L06.** A transaction MUST contain at least one input.
**L07.** A transaction MUST contain at least one output.
**L08.** <span class="small">A transaction's serialized size excluding witness flags and data MUST NOT exceed 1,000,000 bytes.</span>
**L09.** All transaction output amounts MUST be non-negative.
**L10.** The sum of a transaction's output amounts MUST NOT exceed 21,000,000 coins.
**L11.** A transaction's inputs MUST NOT contain duplicate outpoints.
**L12.** A coinbase's sig script size MUST be between 2 and 100 bytes inclusive.
**L13.** A non-coinbase transaction's inputs MUST have non-null previous outputs.

---

# 3. Contextual Rules

**C01.** All transactions in the block MUST be final given the block height and locktime rules.
**C02.** A pre-SegWit block MUST NOT contain any witness data.
**C03.** A block’s total weight MUST NOT exceed 4,000,000 weight units.
**C04.** From BIP34: A coinbase's sig script MUST begin by pushing the block height.
**C05.** From BIP141: A block containing witness data MUST contain a witness commitment.
**C06.** <span class="small">From BIP141: A post-Segwit block containing a witness commitment MUST contain a witness nonce.</span>
**C07.** <span class="small">From BIP141: A post-SegWit block containing a witness commitment MUST commit to its witness Merkle root and nonce.</span>


---

# 4. Spending Rules

**S01.** <span class="small">Transaction outputs MUST NOT give rise to outpoints that reference existing unspent outputs, except in blocks listed in Table 2.</span>
**S02.** A non-coinbase input MUST reference an output created in a preceding transaction.
**S03.** A non-coinbase input MUST NOT reference an output that was spent in a preceding transaction.
**S04.** The total signature-operation cost over all transactions MUST NOT exceed 80,000.
**S05.** The total amount in coinbase outputs MUST NOT exceed the block reward.
**S06.** <span class="small">The sum of output values in a transaction MUST NOT exceed the sum of all input values being spent.</span>
**S07.** A non-coinbase input MUST satisfy the spent output's locking script.
**S08.** <span class="small">From BIP68: Each input that signals a relative lock-time interval MUST have reached relative finality.</span>
**S09.** Coinbase outputs MUST NOT be spent before 100 blocks after their creation.

---

# The Executable View of Specification

---

# Rule functions

> Each semantic rule is enforced by one pure validation function, with its own unique error code.
> There are no state mutations and no side effects.
> Each rule function tests only for its own invariant.

## Example

**H02.** A header's hash MUST NOT exceed its own proof-of-work target.

```cpp
// A header's hash MUST NOT exceed its own proof-of-work target.
[[nodiscard]] inline Result ValidateProofOfWork(const HeaderValidationContext& context) {
  const auto hash = context.header.ComputeHash();
  const auto target = context.header.GetCompactTarget().Expand();
  if (hash > target) return Error::Header_InvalidProofOfWork;
  return {};  // Success
}
```

---

> Consensus rules have no dependencies on the timechain data representation.


## Example

**H04.** A header timestamp MUST be greater than the median of its 11 ancestor blocks' timestamps.

```cpp
// A header timestamp MUST be greater than the median of its 11 ancestor blocks' timestamps.
[[nodiscard]] inline Result ValidateMedianTimePast(const HeaderValidationContext& context) {
  if (context.header.GetTimestamp() <= context.view.MedianTimePast()) return Error::Header_BadTimestamp;
  return {};
}
```

```cpp
// Represents a read-only view onto the ancestors of a candidate block header.
class HeaderAncestryView {
 public:
  // Returns the MTP (Median Time Past) of the tip. Calls into LastNTimestamps.
  uint32_t MedianTimePast() const;

  // Returns the last `count` ancestor timestamps ending at the given height,
  // ordered from oldest to newest. May return fewer than `count` items if not all exist.
  virtual std::vector<uint32_t> LastNTimestamps(int height, int count) const = 0;
  ...
```

---

### Static Validation Rule Composition

> Rules are statically composed into a compute graph using a simple compile-time algebra.


```cpp
// ## Header Rules
static constexpr auto kHeaderRules = All{
  Rule{ValidatePreviousHash},           // A header MUST reference the hash of a valid parent block.
  Rule{ValidateProofOfWork},            // A header's hash MUST NOT exceed its own proof-of-work target.
  Rule{ValidateDifficultyAdjustment},   // A header's proof-of-work target MUST satisfy the difficulty adj...
  Rule{ValidateMedianTimePast},         // A header timestamp MUST be strictly greater than the median of ...
  Rule{ValidateTimestampCurrent},       // A header timestamp MUST NOT exceed network-adjusted time plus 2...
  Rule{ValidateVersion}                 // A header's version number MUST NOT have been retired by any act...
};
```

```cpp
[[nodiscard]] inline Result ValidateHeader(const protocol::BlockHeader& header,
                                           const protocol::BlockHeader& parent,
                                           const HeaderAncestryView& view,
                                           const int64_t current_time) {
  return Validate(kHeaderRules, MakeHeaderContext(header, parent, view, current_time));
}
```

---

### Static Validation Graph Algebra

`Rule`: Names a pure validation function that is executed to enforce one semantic invariant.
`All`: Groups a list of elements that are all executed before returning.
`Each`: Defines an enumerator and applies the child subgraph to each element in the enumeration.
`With`: Defines a projection function and applies the child subgraph to the projected context.
`From`: Specifies a BIP soft fork and only applies the child subgraph after the soft fork is activated.

```cpp
static constexpr auto kSpendingTransactionRules = All{
  Rule{ValidateOutputsAtMostInputs},// The sum of output values in a transaction MUST NOT exceed the sum of...
  Rule{ValidateScripts},            // A non-coinbase input MUST satisfy the spent output's locking script.
  From(BIP::SequenceLocks,    
    Rule{ValidateSequenceLocks}),   // From BIP68: Each input that signals a relative lock-time interval MU...
  Each{InputsInSpend{}, MakeInputSpendContext{},     
    Rule{ValidateCoinbaseMaturity}  // Coinbase outputs MUST NOT be spent before 100 blocks after their cre...
  }
};
```

---

### A Static Declarative Executable Specification

This statically typed declarative graph is the **executable specification** of the semantic invariants that determine consensus validity of a block in the Bitcoin network. Each `Rule` leaf node names a validation function that returns a unique error code if and only if that property fails to hold.

```cpp
// Block Validation Rules
static constexpr auto kBlockRules = All{
  With{MakeHeaderContext,      kHeaderRules},
  With{MakeEnvironmentContext, All{kLocalRules, kContextualRules}},
  With{MakeBlockSpendContext,  kSpendingRules}
};
```

```cpp
// Block Validation
[[nodiscard]] inline Result ValidateBlock(const protocol::Block& block,
                                          const protocol::BlockHeader& parent,
                                          const HeaderAncestryView& view,
                                          const int64_t current_time,
                                          const ChainOutputsView& unspent) {
  const BlockValidationContext context{block, parent, view, current_time, unspent};
  return Validate(kBlockRules, context);
}
```

---

<!-- _class: code-full -->
<style scoped>
section.code-full pre { --fill: 0.50; }
</style>

```cpp
static constexpr auto kConsensusRules = All{
  With{MakeHeaderContext, All{                // ## Header Rules
    Rule{ValidatePreviousHash},               // A header MUST reference the hash of a valid parent block.
    Rule{ValidateProofOfWork},                // A header's hash MUST NOT exceed its own proof-of-work target.
    Rule{ValidateDifficultyAdjustment},       // A header's proof-of-work target MUST satisfy the difficulty adjustment formula for the timechain.
    Rule{ValidateMedianTimePast},             // A header timestamp MUST be greater than the median of its 11 ancestor blocks' timestamps.
    Rule{ValidateTimestampCurrent},           // A header timestamp MUST be less than or equal to network-adjusted time plus 2 hours.
    Rule{ValidateVersion}                     // A header's version number MUST NOT have been retired by any activated soft fork. (See Table 1.)
  }},
  With{MakeEnvironmentContext, All{ All{      // ## Local Rules
    Rule{ValidateNonEmpty},                   // A block MUST contain at least one transaction.
    Rule{ValidateMerkleRoot},                 // A block’s Merkle root field MUST equal the unique Merkle root of its transactions.
    Rule{ValidateOriginalSizeLimit},          // A block’s serialized size excluding witness flags and data MUST NOT exceed 1,000,000 bytes.
    Rule{ValidateCoinbase},                   // A block's first transaction MUST be its only coinbase transaction.
    Rule{ValidateSignatureOps},               // A block's legacy signature-operation count over all input and output scripts MUST NOT exceed 20,000.
    Each{TransactionsInBlock{}, All{
      Rule{ValidateInputCount},               // A transaction MUST contain at least one input.
      Rule{ValidateOutputCount},              // A transaction MUST contain at least one output.
      Rule{ValidateTransactionSize},          // A transaction's serialized size excluding witness flags and data MUST NOT exceed 1,000,000 bytes.
      Rule{ValidateOutputsNonNegative},       // All transaction output amounts MUST be non-negative.
      Rule{ValidateOutputsSum},               // The sum of a transaction's output amounts MUST NOT exceed 21,000,000 coins.
      Rule{ValidateUniqueInputs},             // A transaction's inputs MUST NOT contain duplicate outpoints.
      Rule{ValidateCoinbaseSignatureSize},    // A coinbase's sig script size MUST be between 2 and 100 bytes inclusive.
      Rule{ValidateInputsPrevout}             // A non-coinbase transaction's inputs MUST have non-null previous outputs.    
    }}}, 
  All{                                        // ## Contextual Rules
    Rule{ValidateTransactionFinality},        // All transactions in the block MUST be final given the block height and locktime rules.
    Rule{ValidateNoWitnessPreSegwit},         // A pre-SegWit block MUST NOT contain any witness data.
    Rule{ValidateBlockWeight},                // A block’s total weight MUST NOT exceed 4,000,000 weight units.
    From(BIP::HeightInCoinbase,
      Rule{ValidateCoinbaseHeight}),          // From BIP34: A coinbase's sig script MUST begin by pushing the block height.
    From(BIP::SegWit, With{MakeWitnessContext, All{
      Rule{ValidateWitnessCommitment},        // From BIP141: A block containing witness data MUST contain a witness commitment.
      Rule{ValidateWitnessNonce},             // From BIP141: A post-Segwit block containing a witness commitment MUST contain a witness nonce.
      Rule{ValidateWitnessMerkle}             // From BIP141: A post-SegWit block containing a witness commitment MUST commit to its witness Merkle root and nonce.
    }})
  }}},
  With{MakeBlockSpendContext, All{            // ## Spending Rules
    Rule{ValidateOutPointsUnique},            // BIP30: Transaction outputs MUST NOT give rise to outpoints that reference existing unspent outputs, except in blocks listed in Table 2.
    Rule{ValidateInputPrevoutsCreated},       // A non-coinbase input MUST reference an output created in a preceding transaction.
    Rule{ValidateInputPrevoutsUnspent},       // A non-coinbase input MUST NOT reference an output that was spent in a preceding transaction.
    Rule{ValidateSigOpCosts},                 // The total signature-operation cost over all transactions MUST NOT exceed 80,000.
    Rule{ValidateBlockSubsidy},               // The total amount in coinbase outputs MUST NOT exceed the block reward.
    Each{SpendsInBlock{}, MakeTransactionSpendContext{}, All{
      Rule{ValidateOutputsAtMostInputs},      // The sum of output values in a transaction MUST NOT exceed the sum of all input values being spent.
      Rule{ValidateScripts},                  // A non-coinbase input MUST satisfy the spent output's locking script.
      From(BIP::SequenceLocks,
        Rule{ValidateSequenceLocks}),         // From BIP68: Each input that signals a relative lock-time interval MUST have reached relative finality.
      Each{InputsInSpend{}, MakeInputSpendContext{}, 
        Rule{ValidateCoinbaseMaturity}}       // Coinbase outputs MUST NOT be spent before 100 blocks after their creation.
    }}
  }}
};
```

---

# From Specification to Implementation

---


# UTXOs: Resolving spent prevouts

## `ChainOutputsView`

- Consensus resolves a block's spent previous outputs only through `ChainOutputsView`.
- The interface specifies semantic properties, without constraining their implementation.
- Consensus only sees this pure interface contract, never a database.


```cpp
class ChainOutputsView {                // Consensus sees only this pure interface.
 public:
  virtual bool QueryOutPointsUnique(const Block&) const = 0;        // S01 / BIP30.
  virtual bool QueryPreviousOutputsCreated(const Block&) const = 0; // S02.
  virtual bool QueryPreviousOutputsUnspent(const Block&) const = 0; // S03.
  virtual std::optional<JoinedSpendRange> Spends(const Block&) const = 0;  // S07.
};
```

<!--Presenter: this interface is the boundary between specification and implementation. The spec fixes the validity conditions; everything below this line is unconstrained — which is what permits the design that follows.-->

---

# UTXO(1): A custom UTXO database

> One concrete instance of the interface, in a separate data layer.

```cpp
namespace hornet::data::utxo {
  class DatabaseView : public consensus::ChainOutputsView { ... };
}  // namespace hornet::data::utxo
```

- **Lock-free** **concurrent** reads; **out-of-order** and **speculative** writes.
- Cheap **erase** from a given height allows reorgs without undo data.
- **Single-page** touch per key -- RAM on query, async disk on fetch.
- **~8 GiB** mainnet RAM footprint.
- Only **3k lines** of modern C++ and no dependencies.
- **15 minutes** vs 167 minutes (**11x**) Core v30.0 to reindex mainnet and validate without scripts.$^*$

<!-- Presenter: Clear requirements and compact implementation resolve brittleness and
unlock engineering improvements.-->

Specification `->` Interface `->` Implementation `->` Testing

<span class="footnote">$^*$Details on delvingbitcoin.org</span>

---

# Script Execution

```cpp
while (const auto instruction = parser_.Next()) Execute(*instruction);
```

- Script parsing separated from opcode logic.
- Self-contained opcode handlers registered in a dispatch table.
- `Context` has virtual machine state, stack, and spend / transaction data.
- `Call` pops stack arguments for its lambda, and pushes its result.

<br/>

```cpp
void OnWithin(const Context& context) {
  context.Call([](int32_t x, int32_t xmin, int32_t xmax) {
    return xmin <= x && x < xmax;
  });
}
// ...
table[Op::Within] = &OnWithin;
```

<!-- Presenter: It's another example of modern C++ structured to be declarative to implement the semantics of a specification, rather than getting bogged down in the plumbing of script parsing and stack handling.-->

--- 

# Arithmetic Opcode Handlers

```cpp
template <auto Fn, typename T> void CallBinary(const Context& context) {
  context.Call([](int32_t lhs, int32_t rhs) { return Fn(static_cast<T>(lhs), static_cast<T>(rhs)); });
}

template <auto Fn> constexpr auto OnBinaryBool = &CallBinary<Fn, bool>;
template <auto Fn> constexpr auto OnBinaryInt = &CallBinary<Fn, int64_t>;

// ..
table[Op::Add]                = OnBinaryInt <std::plus{}>;
table[Op::BooleanAnd]         = OnBinaryBool<std::logical_and{}>;
table[Op::BooleanOr]          = OnBinaryBool<std::logical_or{}>;
table[Op::GreaterThan]        = OnBinaryInt <std::greater{}>;
table[Op::GreaterThanOrEqual] = OnBinaryInt <std::greater_equal{}>;
table[Op::LessThan]           = OnBinaryInt <std::less{}>;
table[Op::LessThanOrEqual]    = OnBinaryInt <std::less_equal{}>;
table[Op::NumEqual]           = OnBinaryInt <std::equal_to{}>;
table[Op::NumNotEqual]        = OnBinaryInt <std::not_equal_to{}>;
table[Op::Subtract]           = OnBinaryInt <std::minus{}>;
```

---

# The Pure View of Specification

---

# Hornet DSL

## A pure functional language for consensus specification

```hornet
Rule ValidateLocalRules(block ∈ Block)
    // L01. A block MUST contain at least one transaction.
    Require NonEmptyBlock:      block.transactions ≠ ∅
    ...
    
    // L03. A block’s serialized size excluding witness flags and data MUST NOT exceed 1,000,000 bytes.
    Require OriginalSizeLimit:  |SerializeNoWitness(block)| <= 1,000,000
    ...

    Require CoinbaseFirst:      IsCoinbase(block.transactions.first)
    Require UniqueCoinbase:     ∃! tx ∈ block.transactions : IsCoinbase(tx)
    ...

    // L05. A block's legacy signature-operation count over all input and output scripts MUST NOT exceed 20,000.
    Require SigOpLimit(block)
  ```

---

# Language features
- `Rule`s compose; each `Require` statement is named and must return true.
- Compact mathematical sets and operators `∃ ∀ Σ ∈ ⧺`
- Designed for clarity and compactness, for reading, auditing, and proofs.

Example

```hornet
// L05. A block's legacy signature-operation count over all input and output scripts MUST NOT exceed 20,000.
Rule SigOpLimit(block ∈ Block)

    Let SigOpCost : (op ∈ OpCode) -> int32 |-> 
      ⎧  1  if op ∈ {Op_CheckSig,      Op_CheckSigVerify     },
      ⎨ 20  if op ∈ {Op_CheckMultiSig, Op_CheckMultiSigVerify},
      ⎩  0  otherwise

    Require Σ SigOpCost(inst.opcode)
            ∀ inst ∈ script.instructions
            ∀ script ∈ tx.inputs.scriptSig ⧺ tx.outputs.scriptPubKey
            ∀ tx ∈ block.transactions
        ≤ 20,000
```

---

# Future work

- Minor changes to rule numbering / naming / grouping
- Large-scale differential and LLM-targeted testing
- Continuous adversarial cross-client consensus testing
- Script execution rules
- DSL completion and interpreter
- Efficient full IBD
- DSL compiler for runtime backends
- Formal verification

<!-- Personal part-time project alongside full-time employment -->

---

# Conclusion

- Consensus rules can be defined **explicitly** in a **formal specification**.
- **Declarative** specification guards against
  - the curse of **brittleness**,
  - the risk of **chain splits**,
  - client **centralization**; 
- and enables
  - targeted consensus **test coverage**,
  - a **precise reference** to code against,
  - **modern engineering** design principles.


---

<!-- _class: title -->

<img class="banner" src="banner.png" />

<br/>
<br/>

<div class="orange">
<center>Bitcoin consensus is a protocol.</center><br/>
<center>Hornet gives it a specification.</center>
</div>

<br/>
<div class="footnote">
https://hornetnode.org<br/>
https://github.com/tobysharp/hornet
</div>
# What is Hornet?

Hornet is a minimal, executable specification of Bitcoin's consensus rules, expressed both in declarative C++ and in a purpose-built domain-specific language.

It is implemented as a suite of modular, dependency-free, modern C++ libraries and includes a lightweight node capable of Initial Block Download (IBD).

Designed for clarity and speed, Hornet provides a highly optimized concurrent validation pipeline with a custom UTXO engine, while all consensus logic remains clearly encapsulated by the declarative specification.

### Hornet Consensus Library

A minimal, executable specification of Bitcoin's consensus rules, expressed in declarative modern C++ and in the Hornet domain-specific language (DSL), providing a clear, encapsulated, and testable rule engine, free of side effects, state transitions, and mutable data.

### Hornet DSL

A purpose-built domain-specific language that encodes Bitcoin's consensus rules in a pure, declarative, mathematical form, enabling formal reasoning and automatic generation of corresponding validation code in other languages.

### Hornet UTXO Engine

A low-level, high-performance custom database for indexing unspent transactions, featuring lock-free concurrency, age-stratified LSM-style tiers, reorg-safe semantics, and out-of-order operation. 

### Hornet Node

A lightweight node built atop the consensus and UTXO libraries, currently supporting full IBD through a concurrent validation pipeline and designed for future extensibility into mempool, policy, and multi-peer networking.


## Hornet Architecture

Hornet is built on a strict, acyclic layering model that separates protocol definitions, consensus logic, data structures, and node behavior into cleanly isolated components. Each layer depends only on those below it, preserving clarity, correctness, and modularity across the entire system.

### Layered Structure

Hornet enforces a disciplined hierarchy of namespaces and modules. 

The lower layers define protocol types and serialization; above them sit pure consensus rules; above that, the data structures that maintain chain state. At the top, the node's networking, dispatch, and sync logic. This one-way dependency graph ensures that consensus logic is never entangled with node internals such as chain representation, and that each layer remains independently testable and auditable.

![](layers.svg)

> Figure: Hornet's strict, acyclic layering model. Each layer may depend only on those below it, ensuring modularity, clarity, and clean separation of concerns.

### Consensus Layer

At the core of Hornet is a minimal, executable specification of Bitcoin's header, transaction, and block validation rules. 

These rules are expressed as pure, declarative functions over immutable contexts: each rule enforces a single invariant, has no side effects, and is fully deterministic. Rule sets compose into ordered validation phases that mirror Bitcoin Core's behavior while remaining compact, explicit, and easily verifiable. This purity makes consensus logic suitable for code generation, automated testing, and formal reasoning.

```cpp
const std::array ruleset = {
    Rule{ValidatePreviousHash},
    Rule{ValidateProofOfWork},
    Rule{ValidateDifficultyAdjustment},
    Rule{ValidateMedianTimePast},
    Rule{ValidateTimestampCurrent},
    Rule{ValidateVersion}
};
```

### Hornet DSL Integration

Hornet DSL is a restricted, functional, and mathematically expressive language designed to encode Bitcoin’s consensus rules with maximal precision and minimal ambiguity. 

The language forbids mutable variables, side effects, hidden state, and loops, ensuring that every construct is a pure, total, and fully deterministic function over explicit inputs. This strict functional discipline makes the DSL not only concise and readable but also amenable to static analysis, symbolic reasoning, and eventual formal verification.

Its use of mathematical notation -- tuple comprehensions, bounded ranges, structural recursion, and guarded expressions -- reflects its purpose as a canonical specification: something to be read, trusted, and verified, rather than frequently modified. By constraining expression to “what defines consensus” rather than “how to compute it,” Hornet DSL provides a modern, implementation-neutral foundation from which executable consensus code can be generated and validated across clients.

The long-term goal is for Hornet DSL to serve as the canonical specification from which formally verified consensus libraries in C++, Rust, Go, and other languages can be automatically generated or proved consistent.

```hornet
// The total number of signature operations in a block MUST NOT exceed the consensus maximum.
Rule SigOpLimit(block ∈ Block)
    Let SigOpCost : (op ∈ OpCode) -> int32 
    |-> ⎧  1  if op ∈ {Op_CheckSig,      Op_CheckSigVerify     },
        ⎨ 20  if op ∈ {Op_CheckMultiSig, Op_CheckMultiSigVerify},
        ⎩  0  otherwise
    Require Σ SigOpCost(inst.opcode)
            ∀ inst ∈ script.instructions
            ∀ script ∈ tx.inputs.scriptSig ⧺ tx.outputs.scriptPubKey
            ∀ tx ∈ block.transactions
        ≤ 20,000
```
> Figure: A compact, declarative Hornet DSL rule for the maximum permitted SigOp cost per block.

### Data Layer

Hornet uses custom data structures optimized for Bitcoin's linear-plus-forked chain topology. 

The `ChainTree<T>` data structure stores the main chain in a flat array while representing forks as a lightweight forest. This design yields extremely high memory locality, minimal overhead, and fast access to ancestor information. Metadata is stored in *sidecars* -- parallel structures that mirror the chain-tree layout. Each sidecar maintains its own data (e.g. validation status) while reorgs automatically propagate through all sidecars in lockstep. This separation of concerns keeps consensus code free from storage concerns while supporting precise, efficient reorganizations.

![](chaintree.svg)
> Figure: Hornet's chain-tree representation. The main chain is stored as a flat array, while recent forks form a lightweight forest attached near the tip. This hybrid structure minimizes memory footprint, improves locality, and provides a natural basis for efficient reorganizations and metadata sidecars.

### UTXO Engine

Hornet includes a custom, high-performance UTXO database designed for concurrent, high-throughput, out-of-order validation. 

It provides lock-free concurrency for multiple readers and a single writer. It features native support for efficient reorg handling and out-of-order operations. Disk contention is minimized with efficient batching and high queue-depth async I/O requests. The index uses age-stratified, LSM-style tiers, compacted by concurrent background threads.

The engine integrates cleanly with the validation pipeline without the consensus library taking any dependency on the UTXO database layer. 

### Node Layer

Hornet Node is currently a lightweight client implementation that sits atop the consensus and data layers. It features a modular network protocol loop, fair message scheduling, strongly typed message dispatch, and a clean separation of parsing, verification, and state updates.

Today, Hornet supports single-peer Initial Block Download through a concurrent validation pipeline. Its architecture is deliberately extensible towards multi-peer networking, policy enforcement, and mempool logic.

![](sync.png)
> Figure: Hornet Node's in-browser interface showing live Initial Block Download (single peer). This UI is a development tool used to visualize state, performance, and progress during operation.

### Design Principles

Across all layers, Hornet adheres to a coherent set of design principles:

- Declarative, immutable consensus logic with no side effects
- Strict layering and separation of concerns
- Clear, concise, idiomatic modern C++
- High performance through data-oriented design
- Deterministic, testable, implementation-neutral semantics
- Modular composition and zero external dependencies

These principles give Hornet a uniquely clean, compact, and comprehensible architecture while preserving full compatibility with Bitcoin Core's consensus semantics.


## Summary

Hornet currently functions as a consensus specification and validator with full IBD, and is evolving toward a complete, modern, high-performance Bitcoin client.

For more details, read the paper

Hornet Node and the Hornet DSL: A Minimal, Executable Specification for Bitcoin Consensus, \
T. Sharp, September 2025. \
https://hornetnode.org/paper.html \
https://arxiv.org/abs/2509.15754


---

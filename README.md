# Hornet Node

## What is Hornet?

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

## Why Hornet?

Bitcoin is defined by its consensus rules, yet those rules have never existed as a clear, independent specification. Hornet begins from the premise that the protocol deserves a precise, executable foundation separate from any particular codebase, so that Bitcoin's software can evolve and diversify safely even as its rules ossify.

Without an independent specification, code changes become risky, and the long-term effect is software centralization: a single codebase accumulates legacy designs, historical constraints, and layers of defensive caution. Refactors and rewrites are discouraged because any deep change must preserve implicit behavior that only lives in that one implementation. Alternative clients may begin as forks of Core, inheriting its structure, assumptions, and technical debt -- yet without its review process. Hornet takes the opposite approach: a clean, modern implementation of consensus from first principles, designed explicitly around clarity, modularity, and an independent ruleset rather than inherited code paths.

Hornet expresses Bitcoin's consensus rules as a minimal, executable specification in declarative C++ and in the Hornet domain-specific language. This makes consensus explicit, compact, and testable. Crucially, Hornet's consensus layer is implemented as a standalone library: a pure, side-effect-free rule engine with well-defined inputs and outputs, isolated from node internals such as data structures, mempool behavior, storage layout, or networking. Any node implementation can link against this library or (in the future) generate equivalent code in another language from the specification compiler. This separation gives the ecosystem something it has never had: a clear definition of consensus independent of any particular implementation.

This clarity sharpens the boundary between consensus and policy. Mempool rules, relay preferences, and filtering choices are legitimate areas of client variation, but without a crisp separation they can be mistaken for protocol changes. A specification-driven approach makes this distinction objective and unambiguous.

An explicit specification enables the healthy software diversity that mature ecosystems depend on. Bitcoin should be able to support multiple high-quality, independent implementations -- written in different languages, optimized for different priorities, structured around different design philosophies -- without risking fragmentation. Hornet provides that foundation by anchoring consensus semantics in a shared, rigorous specification.

A minimal and well-structured specification strengthens everything downstream: testing becomes simpler, differential analysis becomes systematic, formal verification becomes attainable, and onboarding new contributors becomes easier. Hornet's compact, layered design also makes it uniquely suited for education and experimentation, allowing developers to study, test, and extend consensus logic without navigating all the complexity of Bitcoin Core internals.

Hornet’s goal is to specify Bitcoin consensus precisely, and to provide a clean, modern consensus library that other clients can build on. This allows the software ecosystem to modernize, diversify, and innovate safely, without ever compromising the correctness of the consensus rules.


## Hornet and libbitcoinkernel

One related effort toward isolating consensus logic is *libbitcoinkernel*, an ongoing refactoring effort within Bitcoin Core that extracts Core's validation code into a library with a C API. This improves modularity for Core and allows other clients to call into Core's consensus engine directly. However, because it is carved out of the existing codebase, libbitcoinkernel necessarily retains Core's internal data structures, architectural assumptions, and dependencies, including its LevelDB-based storage layer and threading model.

Hornet takes a different path. Instead of reorganizing Core's existing code, Hornet is a fresh, first-principles implementation of Bitcoin's consensus rules, expressed as a minimal and declarative specification in modern C++ and in the Hornet DSL. This keeps Hornet inherently more agile: the codebase is small, clean, elegant, and unconstrained by legacy technical debt. It explores what Bitcoin code could look like if implemented from scratch today using the best modern engineering principles and idiomatic modern C++.

Hornet also exposes a much smaller consensus surface area. In Bitcoin Core, the consensus engine is intertwined with the `CCoinsView` and `CCoinsViewCache` UTXO layers, LevelDB-backed storage, `CBlockIndex`, `CChainState`, `ChainstateManager`, and the broader block index and chainstate infrastructure. In Hornet, all of these components live outside the consensus library as implementation details, leaving consensus itself small, pure, and easy to reason about.

Where libbitcoinkernel exposes Core’s consensus behavior for reuse, Hornet aims to specify consensus in an implementation-neutral form. Hornet’s consensus layer is a standalone library in the architectural sense: a pure, side-effect-free rule engine with no dependency on any particular UTXO database, chain representation, or networking stack. Node developers can pair it with whatever storage backend or runtime architecture they prefer, and — with the future DSL compiler — can generate equivalent consensus logic in other languages entirely.

In short, Hornet’s specification-driven design aims to provide a modern and future-oriented foundation for multiple independent clients, enabling faster development, safer innovation, and greater diversity across the Bitcoin ecosystem.


## Hornet Architecture

Hornet is built on a strict, acyclic layering model that separates protocol definitions, consensus logic, data structures, and node behavior into cleanly isolated components. Each layer depends only on those below it, preserving clarity, correctness, and modularity across the entire system.

### Layered Structure

Hornet enforces a disciplined hierarchy of namespaces and modules. 

The lower layers define protocol types and serialization; above them sit pure consensus rules; above that, the data structures that maintain chain state. At the top, the node's networking, dispatch, and sync logic. This one-way dependency graph ensures that consensus logic is never entangled with node internals such as chain representation, and that each layer remains independently testable and auditable.

![](docs/layers.svg)

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

![](docs/chaintree.svg)
> Figure: Hornet's chain-tree representation. The main chain is stored as a flat array, while recent forks form a lightweight forest attached near the tip. This hybrid structure minimizes memory footprint, improves locality, and provides a natural basis for efficient reorganizations and metadata sidecars.

### UTXO Engine

Hornet includes a custom, high-performance UTXO database designed for concurrent, high-throughput, out-of-order validation. 

It provides lock-free concurrency for multiple readers and a single writer. It features native support for efficient reorg handling and out-of-order operations. Disk contention is minimized with efficient batching and high queue-depth async I/O requests. The index uses age-stratified, LSM-style tiers, compacted by concurrent background threads.

The engine integrates cleanly with the validation pipeline without the consensus library taking any dependency on the UTXO database layer. 

### Node Layer

Hornet Node is currently a lightweight client implementation that sits atop the consensus and data layers. It features a modular network protocol loop, fair message scheduling, strongly typed message dispatch, and a clean separation of parsing, verification, and state updates.

Today, Hornet supports single-peer Initial Block Download through a concurrent validation pipeline. Its architecture is deliberately extensible towards multi-peer networking, policy enforcement, and mempool logic.

![](docs/sync.png)
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

Hornet currently functions as a consensus specification and validator with full IBD, and is evolving toward a complete, modern, high-performance Bitcoin client, with the Hornet DSL providing a long-term path toward language-neutral, formally verified consensus libraries.

For more details, read the author's paper:

Hornet Node and the Hornet DSL: A Minimal, Executable Specification for Bitcoin Consensus, \
T. Sharp, September 2025. \
https://hornetnode.org/paper.html \
https://arxiv.org/abs/2509.15754


---

# Hornet Funding Proposal

## Project Stewardship

<img src="portrait.jpg" width="150px" style="float:left; margin: 0 10px 10px 0;"/> Hornet Node, the Hornet DSL, and all supporting libraries are entirely conceived and developed by Toby Sharp, a mathematician, computer scientist, and real-time systems engineer with 30 years of professional experience.

Since 2022, Toby has served as the Lead Architect for Perception Systems in Google's Android XR division, where he recently designed and developed a pioneering real-time parametric model-fitting system used for vision-based hand and face tracking in Google's XR platform (e.g., Samsung Galaxy XR device). 

Before joining Google, he spent 17 years as a Principal Software Scientist at Microsoft, where he co-authored state-of-the-art academic papers and developed breakthrough real-time vision technologies enabling products including Microsoft Kinect and Microsoft HoloLens. He is the lead developer of Microsoft's Azure Kinect Body Tracking SDK and KinectFusion SDK, as well as computer vision libraries used by multiple Microsoft products. 

Toby has over 70 publications on [Google Scholar](https://scholar.google.com/citations?user=OOcllDwAAAAJ), with more than 14,000 citations and an h-index of 34.

His work has been recognized with the Royal Academy of Engineering's MacRobert Award (Gold Medal), the IEEE Computer Vision Foundation's Longuet-Higgins Prize, the IEEE CVPR Best Paper Award, and the ACM SIGCHI Honorable Mention Award. He is also the inventor or co-inventor on over 40 patents, with more than a dozen Microsoft technology-transfer awards for successfully productizing advanced research.


## Current Funding

Hornet began as a spare-time research project in May 2025, developed alongside a full-time senior engineering role at Google. In less than six months of evenings and weekends, he implemented a *de novo* declarative, executable specification for Bitcoin consensus that is nearly complete, and syncs IBD within a few hours against a single peer. In September he wrote up his results in the paper, *Hornet Node and the Hornet DSL: A Minimal, Executable Specification of Bitcoin Consensus*.

Since then, he has almost completed a novel, custom, concurrent UTXO database that enables high-performance, lock-free unspent transaction queries and will be used by the consensus engine through an opaque interface.

Direct costs so far have been minimal, limited to a one-time $5k investment in a development workstation, and modest travel costs to Bitcoin conferences.

As of 11/23/25, a total of 0.01047424 BTC ($906) has been donated to the project, which can be tracked at the donation address [33Y6TCLKvgjgk69CEAmDPijgXeTaXp8hYd](https://mempool.space/address/33Y6TCLKvgjgk69CEAmDPijgXeTaXp8hYd).

While progress on Hornet has been quick and encouraging, continuing at the current spare-time pace is not sustainable. A long-term commitment requires choosing between focusing fully on Hornet development or scaling the project back in favor of primary professional responsibilities. This proposal outlines the support needed to pursue Hornet as a full-time initiative and the impact that dedicated funding would enable for the Bitcoin ecosystem.


## Current Project Status

Below is a concise summary of the project’s current capabilities and development milestones. Significant progress has been made in a short time toward a disciplined, modern, specification-driven Bitcoin client. The project currently includes:

### 1. A Nearly Complete Declarative Consensus Specification

Hornet now implements a *de novo* executable specification for all major Bitcoin consensus rules -- headers, transactions, block structure, and context -- expressed in declarative C++ as composable rules with strict immutability and side-effect-free semantics. For each putative block, consensus answers the singular question, "Is this block a valid addition to the known timechain?" This consensus library successfully validates the entire historical chain during IBD. Only spending and script validation are yet to be completed to be fully consensus correct.

### 2. Prototype Hornet DSL and Compiler Foundations

The Hornet Domain-Specific Language is sketched at the prototype level and can already express consensus rules in a pure, mathematical form. Next steps for this component are to complete the specification and the language grammar, and to parse the DSL specification with a customized compiler.

### 3. Functional IBD Pipeline (Single Peer)

Hornet Node performs full IBD from genesis to tip against a single peer, validating consensus rules with a serial validation pipeline. This is in the process of being upgraded to a maximally concurrent validation pipeline with out-of-order operations for optimal parallelism.

### 4. Novel Concurrent UTXO Database (Near Completion)

A custom lock-free UTXO database engine has been designed and nearly completed. The engine integrates cleanly with the consensus library through an opaque interface, with no consensus-layer dependency on storage. Its architecture supports:

- multiple concurrent reader threads,
- a single-writer model for maximal throughput,
- age-stratified LSM-style index tiers,
- out-of-order operation for minimizing pipeline stalls,
- built-in reorg / undo support,
- optimal fetch I/O with deep async queues for NVMe SSDs.

The UTXO database is expected to be finished, tested, and integrated by end of 2025.

### 5. ChainTree Data Structure & Metadata Sidecars

The `ChainTree` structure is complete and in production use within Hornet Node. It stores the main chain as a flat array with a lightweight forest for forks, achieving extremely high locality and efficient reorganizations. Metadata sidecars mirror the chain-tree structure, enabling clean separation between consensus logic, chain storage, and auxiliary data. The entire data model exists outside of the consensus layer, and therefore may be modified without affecting consensus code. This is a powerful property of structured encapsulation that Bitcoin Core's current architecture does not provide.

### 6. Lightweight Node Implementation

Hornet includes a functioning node layer with:
- a fair, modular protocol loop,
- strongly typed message dispatch with a Visitor pattern,
- a clean separation of parsing, validation, and state updates,
- real-time browser-based UI for observing events and metrics.

The node is deliberately minimal but operational as a development and validation environment.


## Roadmap (FY26)

Hornet’s next development steps are focused on completing full consensus correctness, strengthening performance, and establishing a clean, independent specification layer. The project’s upcoming milestones are:

### 1. UTXO Engine Completion, Benchmarking, and Integration

Finalize the custom concurrent UTXO database, implement all remaining internal operations, run micro-benchmarks and full I/O stress tests, and integrate the engine into the validation pipeline for end-to-end IBD testing.

### 2. Signature Validation (libsecp256k1, Schnorr)

Integrate libsecp256k1 for ECDSA verification and add full BIP340 Schnorr signature support. Implement clean abstractions for deterministic, stateless signature checks within the declarative consensus framework.

### 3. Complete Script Execution and Validation

Finish implementing the remaining Bitcoin Script opcodes, ensure precise Core-equivalent edge-case behavior, integrate script evaluation into block and transaction validation, and confirm full compatibility against historical block data.

### 4. Concurrent Validation Pipeline 

Refine and stabilize the full multi-stage concurrent validation pipeline, including out-of-order operations, queue coordination, and safe parallelism. Improve clarity, determinism, and instrumentation for debugging and performance analysis.

### 5. Open-Source Release of the C++ Consensus Library

Prepare the declarative consensus engine for public release by cleaning interfaces, adding documentation, test coverage, reproducible builds, and high-level usage examples. Publish the library under a permissive open-source license. Include a test app that reads blocks from disk or from a peer and validates them in order to the tip.

### 6. Complete Consensus Specification in Hornet DSL

Finish writing the full consensus ruleset in Hornet DSL, including header rules, transaction rules, block structure, contextual validation, and script semantics. This becomes the canonical, implementation-neutral specification.

### 7. Initial Hornet DSL Parser / Compiler

Develop the first version of the Hornet DSL parser and compiler to explore and transform Hornet DSL representations. Output will initially target internal IR structures and simple C++ generation for comparative testing.

### 8. Complete Multi-Peer IBD

Extend the node layer from single-peer operation to multi-peer synchronization. Implement DoS resistance and fault-tolerant handling of inconsistent or misbehaving peers.

### 9. Add Steady-State Chain Updates

Support real-time steady-state operation after IBD: efficient processing of new blocks as they arrive, with correct reorg management, orphan handling, and continuous update of the UTXO set and validation status.


## Deliverables & Success Criteria (1 Year)

Hornet’s FY26 development is centered on two primary outputs: a high-performance, fully open-source C++ consensus library, and a compact, implementation-neutral Hornet DSL specification. These form the foundation of Hornet’s long-term mission as a verifiable, formally expressible Bitcoin consensus engine.

### 1. Hornet C++ Consensus Library (Open Source)

A complete, declarative, side-effect-free, dependency-free implementation of Bitcoin’s consensus rules in modern C++.

Includes:

- Functioning IBD test applications capable of validating the full chain from genesis to tip.
- Comprehensive unit tests, correctness tests, and differential checks.
- Clear documentation, public APIs, and reproducible builds.

Success:
The library validates the entire historical blockchain in less time than Bitcoin Core, and provides a clean, maintainable foundation for developers, researchers, and independent clients.

### 2. Complete Hornet DSL Specification

A compact, mathematically precise specification of Bitcoin’s consensus rules.

Includes:

- A fully defined grammar and syntax, covering all consensus phases.
- An early parser / compiler capable of interpreting the DSL into internal representations or simple executable code.
- [Stretch] Execution via a minimal VM or generated C++ code.
- [Stretch] Full-chain validation using the DSL-derived execution path, verifying equivalence with the C++ implementation.

Success:
The DSL serves as a canonical, implementation-neutral description of Bitcoin consensus -- readable by humans, analyzable by tools, and suitable for future formal verification.


## Funding Requirements

### Option A: Full-time Funding (Preferred)

**Annual compensation: $330,000 salary + 25% performance bonus + BTC vesting package + benefits.**

#### Compensation Structure

To work full-time on Hornet, the author would be stepping away from a highly compensated and technically senior L7 IC role at Google and forfeiting approximately $2M in unvested $GOOG stock. In standard industry practice, particularly at the principal / staff / distinguished IC level, a transition to a new company is accompanied by:
- a buyout of unvested equity, and
- a forward-looking equity/asset vesting schedule,
so that the engineer is not financially disincentivized from the move.

To replace the forfeited Google compensation and to align long-term incentives:

#### BTC Vesting Package:
- A BTC-denominated vesting package structured as an equity replacement for 6,864 forfeited unvested $GOOG shares,
- vested on a front-loaded schedule mirroring Google’s existing vesting timeline,
- plus $500,000 converted to BTC, vested over 4 years (2026 to 2030) as a forward-looking incentive.

#### Salary and Bonus
- $330,000 base salary (matching existing level, 0% increase)
- 25% target annual bonus
  - reduced for below-target deliverables,
  - increased for above-target outcomes.

#### Benefits
- Top-tier medical, dental, and vision insurance for self and partner
- Hardware and cloud compute for large-scale correctness testing
- Travel and expenses for 3-4 relevant conferences
- Incidental expenses

#### Immigration Support
- Reference letters and sponsorship documentation supporting continued residence and work authorization in the USA.

### Option B: Part-time Funding (50% Commitment)

**Annual compensation: $206,250 salary + $250k in BTC**

#### Compensation Structure

In this option, the author would remain employed with Google on a part-time basis, with a primary commitment to his employer, who would be paying the majority of compensation and all benefits.

Time and focus would be divided to allow 50% of full-time to be dedicated to Hornet. This allows for progress on Hornet at a slower rate, and deliverables would be scaled down to match. However, it represents good value for the funding partner.

#### BTC Vesting Package:
- $250k converted to BTC and vested over 4 years (2026 to 2030).

#### Immigration Support
- Reference letters and sponsorship documentation supporting continued residence and work authorization in the USA.

## Impact
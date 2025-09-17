![](banner.png)

# Hornet Node and the Hornet DSL:
### *A Pure, Executable Specification for Bitcoin Consensus*

## Toby Sharp, September 2025

## 1. Abstract

Bitcoin's consensus rules are encoded in the implementation of its reference client: "The code is the spec." Yet the nature of this code is unsuitable to formal verification due to side effects, mutable state, concurrency, and legacy design.

An independent formal specification for Bitcoin could enable verification between versions of the reference client, and against novel client implementations, enhancing software decentralization without the risk of bug-induced consensus splits. Such a specification was widely regarded as intractable or impractical due to the complexity of consensus logic.

We demonstrate a structured, executable, declarative specification of Bitcoin consensus rules, and use this to sync mainnet to tip in 3 hours using a single thread. We also introduce the Hornet Domain-Specific Language (DSL) specifically designed to encode these rules unambiguously for execution, enabling formal reasoning, consensus code generation, and AI-driven adversarial testing.

We introduce our spec-driven client Hornet Node as a modern and elegant complement to the reference client. Its clear, idiomatic style makes it suitable for education, while its performance makes it ideal for experimentation. We highlight architectural contributions such as its layered design, novel efficient data structures, and strong separation of concerns, supported by real-world code examples. We argue that Hornet Node and Hornet DSL together provide the first credible path toward a pure, formal, executable specification of Bitcoin consensus.

<table>
<tr>
<th>Declarative C++</th>
<th>Hornet DSL</th>
</tr>
<tr>
<td>

```cpp
// Performs contextual block validation, aligned with Core's ContextualCheckBlock function.
[[nodiscard]] 
inline auto ValidateBlockContext(const protocol::Block& block,
                                 const int height,
                                 const AncestorTimestampsView& ancestry)
                              -> std::expected<void, BlockError> {
  static constexpr std::array ruleset = {
    // All transactions in the block MUST be final given the block height and locktime rules.
    Rule{ValidateTransactionFinality},                            
    // From BIP34, the coinbase transaction’s scriptSig MUST begin by pushing the block height.
    Rule{ValidateCoinbaseHeight,        BIP::HeightInCoinbase },  
    // From BIP141, the coinbase transaction MUST include a valid witness commitment for blocks containing witness data.
    Rule{ValidateWitnessCommitment,     BIP::SegWit           },  
    // A block’s total weight MUST NOT exceed 4,000,000 weight units.
    Rule{ValidateBlockWeight}
  };                                   
  BlockValidationContext context{block, height, ancestry};
  return ValidateRules<BlockError>(ruleset, context.height, context);
}
```
</td><td>

```cpp
// Performs contextual block validation, aligned with Core's ContextualCheckBlock function.
@rule @phase("block_context") 
rule ValidateBlockContext(block: Block, 
                          height: uint32, 
                          past_timestamps: array<uint32, 11>)
                       -> BlockError? {

  // All transactions in the block MUST be final given the block height and locktime rules.
  require ValidateTransactionFinality
  // From BIP34, the coinbase transaction’s scriptSig MUST begin by pushing the block height.
  @bip(BIP34) require ValidateCoinbaseHeight
  // From BIP141, the coinbase transaction MUST include a valid witness commitment for blocks containing witness data.
  @bip(BIP141) require ValidateWitnessCommitment
  // A block’s total weight MUST NOT exceed 4,000,000 weight units.
  require ValidateBlockWeight
}



```

</td></tr></table>

> **Figure 1.** *Where we are and where we're going: On the left is Hornet Node's executable, declarative constrained C++ implementation of Bitcoin's contextual block validation rules. These rules (with others shown elsewhere) are used today to sync and validate all mainnet headers in under 3 seconds, and all blocks in a few hours in a single thread. On the right is its intended evolution: Hornet DSL, a purpose-built domain-specific language designed to express Bitcoin's consensus rules for composability, semantic reasoning, and eventual formal verification. Note for example the `@bip` annotations to selectively apply rules dependent on reaching the relevant activation height. See text for details.*


## 2. Background

### 2.1 The Need for Client Diversity
Recently it was reported that nearly 20% of Bitcoin nodes are now Knots clients. While a variety of clients is an inevitable sign of healthy decentralization, we currently lack a formal specification that can be robustly implemented, tested, and ideally formally proven. This presents the risk that buggy clients will experience consensus splits, provoking user confusion, media FUD, and potentially hard forks. 

### 2.2 Protocol Ossification vs Code Evolution
While there may be strong arguments for the ossification of the protocol, the same cannot be said for any codebase. Software must be maintained to be able to run on current hardware and operating systems, to fix bugs, and to adhere to design principles. Moreover, programming languages evolve, and each generation will have different ways to express logic. To attract talented developers of the future, the reference client should also allow for refactoring and improvement. 

### 2.3 The Role of Formal Specification
The above risks could be substantially mitigated by prioritizing a pure specification of consensus rules separate from its implementation. Such a spec would enable plaintext readability, LLM reasoning, full-coverage automated testing, and eventually formal verification. The end goal would be a formal proof that a given client is consensus-correct.

### 2.4 The Limitations of Formal Verification
While the goal of formal provability for a client is desirable, it is not possible to do formal reasoning directly against the reference client today. Theorem provers like Coq work with small, constrained programs to compile and transform expression trees through a vast high-dimensional space. On the other hand, Bitcoin Core is a large, entangled imperative codebase with side effects, state mutation, and concurrency.


## 3. Specifying Consensus

Our response to the challenges is a declarative, executable specification of Bitcoin's consensus rules, designed under language constraints such as pure functions, explicit state transitions, immutability by default, and a strict avoidance of side effects or concurrency.

We first show how this specification drives validation in Hornet Node, our *de novo*, modern C++ client. We then introduce the Hornet DSL: a canonical, implementation-neutral domain-specific language for Bitcoin consensus, designed to be easily and unambiguously parsed, audited, and reasoned about--whether by humans, LLMs, or theorem provers.

We then outline Hornet Node’s other architectural contributions and show selected code examples, concluding with a discussion of future work.
 

## 4. Hornet Node

Hornet Node is a consensus-compatible Bitcoin client designed from the ground up to be modular, rigorous, efficient, and modern. Developed with reference to Bitcoin Core's behavior but without any copied code or external dependencies, it is a solo passion project to express the elegance of the Bitcoin protocol in elegant, idiomatic C++.

A work in progress, Hornet Node currently connects to a single peer, requests and validates mainnet headers and blocks to the tip, using consensus rules that match Bitcoin Core's behavior. It uses novel data structures for timechain data and metadata, and fully supports chain reorganizations.

![](sync.png)
> **Figure 2.** *Hornet Node's interactive web UI showing Initial Block Download (IBD) syncing and validating mainnet headers and blocks against its declarative executable consensus specification.*

Hornet enforces a strict one-way dependency graph (no cycles) between its hierarchical modules or *layers*, each of which is contained in its own folder and namespace. Each layer may only depend on the layers below itself. For example, `hornet::protocol` is the layer that defines protocol-specific types like `BlockHeader`. `hornet::consensus` is a layer above that may access `hornet::protocol`. But `hornet::data`, which contains data structures like the timechain itself is above `hornet::consensus`, so consensus logic has no knowledge of these implementation details. This structural discipline prevents code sprawl and limits dependency surface keeping consensus logic tight.

![](layers.svg)
> **Figure 3.** *A generated layer cake diagram showing Hornet Node's layers / namespaces in stack order. Each layer may only access the layers below it in the diagram.*


### 4.1 Bitcoin Script Code

Before turning to the core consensus validation code, it's worth briefly illustrating the architectural and stylistic approach that Hornet follows throughout. The following example constructs and executes a simple Bitcoin script that evaluates the expression `(21 + 21) == 42`. While this logic is not part of consensus validation, it demonstrates the principles that make the declarative specification possible: pure functional style, composable operations, and clean separation of state and behavior.

```C++
TEST(ScriptTest, RunSimpleScript) {
    // Build a Bitcoin script to evaluate the expression (21 + 21) == 42.
    const auto script = Writer{}.PushInt(21).
                                 PushInt(21).
                                 Then(Op::Add).
                                 PushInt(42).
                                 Then(Op::Equal).Release();

    // Execute the script using the stack-based virtual machine.
    const auto result = Processor{script}.Run();

    // Assert that the script execution completed without error.
    ASSERT_TRUE(result);
    
    // Check the result of execution is 'true'.
    EXPECT_EQ(*result, true);
}
```
> **Figure 4.** *A unit test that builds and executes a Bitcoin Script to evaluate the expression `(21 + 21) == 42`, and checks that it completes execution without error and with the value `True`.*

Within the script execution runtime, each opcode handler is a self-contained callable free function that receives the virtual machine state and current instruction inside a `protocol::script::runtime::Context` object. This modular syle enables unit testing and formal reasoning per-opcode, avoiding the tightly coupled complexity of Core's `EvalScript`.

```C++
// Op::PushSize1 ... Op::PushData4
static void OnPushData(const Context& context) {
  if (context.RequiresMinimal()) detail::VerifyMinimal(context.instruction);
  context.Stack().Push(context.instruction.data);
}

// Op::Add
static void OnAdd(const Context& context) {
  BinaryIntOp(context, [](int64_t a, int64_t b) { return a + b; });
}

// Op::Equal
static void OnEqual(const Context& context) {
  BinaryBitwiseOp(context, [](const auto& a, const auto& b) { 
    return std::ranges::equal(a, b); 
  });
}
```
> **Figure 5.** *Each opcode handler is a self-contained state-free pure function, independently testable. In contrast to Bitcoin Core’s monolithic `EvalScript`, Hornet’s handlers are modular and stateless, making script semantics easier to reason about, audit, and validate against the consensus specification.* 

While script execution is not yet integrated into Hornet's block validation, the runtime shown in Figure 5 is consensus-critical and will ultimately form part of the full specification. Whether expressed in C++ or in the DSL, the structure ensures that Bitcoin Script semantics can be captured with precision, clarity, and auditability. This same design discipline--pure rules, explicit contexts, and modular handlers--carries through into Hornet's consensus-critical header and block validation, to which we now turn.


## 4.2 Header Validation

The consensus rule engine has several validation phases, the first of which is block header validation. The header consensus rules require access to state: for example, to enforce the Median Time Past rule (MTP, BIP113), a header's timestamp must be compared against the median of the past 11 ancestor timestamps. At first glance, this might suggest that consensus logic needs direct access to the header chain or node state, which would violate Hornet's strict layering model and endanger the purity of consensus rules.

Instead, Hornet introduces a minimal abstraction called `consensus::HeaderAncestryView`. This is an interface defined at the consensus layer and implemented by the data layer, allowing validation logic to query exactly the information it needs without breaking dependency boundaries. The result is a layered design that supports pure, side-effect-free rule definitions, even when those rules depend on dynamic chain state.

We now show the first part of the executable declarative C++ specification: header validation.

```C++
// Performs header validation, aligned with Core's CheckBlockHeader and ContextualCheckBlockHeader.
[[nodiscard]] inline auto ValidateHeader(const protocol::BlockHeader& header,
                                         const model::HeaderContext& parent,
                                         const AncestorTimestampsView& view,
                                         const int64_t current_time) 
                                      -> std::expected<void, HeaderError> {
  const std::array ruleset = {
    // A header MUST reference the hash of its valid parent.
    Rule{ValidatePreviousHash},     
    // A header's 256-bit hash value MUST NOT exceed the header's proof-of-work target.
    Rule{ValidateProofOfWork},      
    // A header's proof-of-work target MUST satisfy the difficulty adjustment formula.
    Rule{ValidateDifficultyAdjustment},
    // A header timestamp MUST be strictly greater than the median of its 11 ancestors' timestamps.
    Rule{ValidateMedianTimePast},   
    // A header timestamp MUST be less than or equal to network-adjusted time plus 2 hours.
    Rule{ValidateTimestampCurrent}, 
    // A header version number MUST meet deployment requirements depending on activated BIPs.
    Rule{ValidateVersion}           
  };
  HeaderValidationContext context{header, parent, view, current_time, parent.height + 1};
  return ValidateRules<HeaderError>(ruleset, 0, context);
}
```
>**Figure 6.** *Hornet's executable ruleset that specifies Bitcoin's header validation.*

Header validation in Hornet is defined as an ordered ruleset (Figure 6): a list of pure, composable, testable functions, each responsible for enforcing a single invariant. This function mirrors Bitcoin Core's `CheckBlockHeader()` and `ContextualCheckBlockHeader()` but expresses the logic declaratively and without side effects. Each rule operates solely on its input context, returning either a typed error code or success.

Each validation phase likewise has a ruleset comprising an ordered list of identically typed functions, each optionally tagged with a BIP for conditional operation. The implementation of all rules spans less than 50 lines, and is shown below for completeness.

```C++
namespace hornet::consensus::rules {

using ValidateHeaderResult = std::expected<void, HeaderError>;

namespace detail {
inline bool IsVersionValidAtHeight(const int32_t version, const int height) {
  constexpr std::array<BIP, 4> kVersionExpiryToBIP = {
      BIP34, BIP34,  // v0, v1 retired with BIP34.
      BIP66,         // v2 retired with BIP66.
      BIP65          // v3 retired with BIP65.
  };
  if (version >= std::ssize(kVersionExpiryToBIP)) return true;
  const int index = std::max(0, version);
  return !IsBIPEnabledAtHeight(kVersionExpiryToBIP[index], height);
}
}  // namespace detail

// A header MUST reference the hash of its valid parent.
[[nodiscard]] inline ValidateHeaderResult ValidatePreviousHash(
    const HeaderValidationContext& context) {
  if (context.parent.hash != context.header.GetPreviousBlockHash())
    return HeaderError::ParentNotFound;
  return {};
}

// A header's 256-bit hash value MUST NOT exceed the header's proof-of-work target.
[[nodiscard]] inline ValidateHeaderResult ValidateProofOfWork(
    const HeaderValidationContext& context) {
  if (context.header.ComputeHash() > context.header.GetCompactTarget().Expand())
    return HeaderError::InvalidProofOfWork;
  return {};
}

// A header's proof-of-work target MUST satisfy the difficulty adjustment formula.
[[nodiscard]] inline ValidateHeaderResult ValidateDifficultyAdjustment(
    const HeaderValidationContext& context) {
  if (context.header.GetCompactTarget() !=
      AdjustCompactTarget(context.height, context.parent.data, context.view))
    return HeaderError::BadDifficultyTransition;
  return {};
}

// A header timestamp MUST be strictly greater than the median of its 11 ancestors' timestamps.
[[nodiscard]] inline ValidateHeaderResult ValidateMedianTimePast(
    const HeaderValidationContext& context) {
  if (context.header.GetTimestamp() <= context.view.MedianTimePast())
    return HeaderError::TimestampTooEarly;
  return {};
}

// A header timestamp MUST be less than or equal to network-adjusted time plus 2 hours.
[[nodiscard]] inline ValidateHeaderResult ValidateTimestampCurrent(
    const HeaderValidationContext& context) {
  constexpr int kTimestampTolerance = 2 * 60 * 60;
  if (context.header.GetTimestamp() > context.current_time + kTimestampTolerance)
    return HeaderError::TimestampTooLate;
  return {};
}

// A header version number MUST meet deployment requirements depending on activated BIPs.
[[nodiscard]] inline ValidateHeaderResult ValidateVersion(
    const HeaderValidationContext& context) {
  if (!detail::IsVersionValidAtHeight(context.header.GetVersion(), context.height))
    return HeaderError::BadVersion;
  return {};
}

}  // namespace hornet::consensus::rules
```
>**Figure 7.** *The full ruleset implementation for header consensus validation.*

Let us consider `ValidateProofOfWork` in Figure 6 as an example. The comment line gives the English language plain description of the rule: *"A header's 256-bit hash value MUST NOT exceed the header's proof-of-work target."* The function first computes the SHA256^2 hash of the header, and then comparess its arithmetic value against the 256-bit-expanded compact target resulting from the difficulty rule. If the hash value exceeds the target, the rule returns `InvalidProofOfWork`, otherwise it returns success. All other rules enforce their own specific constraints.

Note that all variables are declared as `const` to enforce immutability. While the validation rules delegate to methods like `BlockHeader::ComputeHash()` etc., these are immutable, deterministic, side-effect-free, and bound to protocol data only, making them effectively pure in the context of consensus specification.

The same declarative style extends beyond headers to encompass transactions and blocks. In the next section we show how these validation phases compose into a complete executable specification of Bitcoin consensus rules that is compact, rigorous, and verifiable.


## 4.3 Bitcoin Consensus As A Declarative Executable Specification

We now show the full rulesets for Bitcoin consensus header validation, block validation, and transaction validation in C++. Note that transaction validation is enforced inside the block rule `ValidateTransactions` via composability. Note also the BIP34 and BIP141 tags to restrict contextual block rules by activation height.

Remarkably, we can express the full set of Bitcoin's header, transaction, and block validation rules in fewer than 50 lines of idiomatic C++, where each rule encapsulates one semantic criterion of consensus. The code below is a fully executable and performant implementation of Bitcoin's validation logic.

```C++
// Performs header validation, aligned with Core's CheckBlockHeader and ContextualCheckBlockHeader.
[[nodiscard]] inline auto ValidateHeader(const protocol::BlockHeader& header,
                                         const model::HeaderContext& parent,
                                         const AncestorTimestampsView& view) {
  const std::array ruleset = {
    // A header MUST reference the hash of its valid parent.
    Rule{ValidatePreviousHash},
    // A header MUST satisfy the chain's target proof-of-work.
    Rule{ValidateProofOfWork},
    // A header's proof-of-work target MUST satisfy the difficulty adjustment formula.
    Rule{ValidateDifficultyAdjustment},
    // A header timestamp MUST be strictly greater than the median of its 11 ancestors' timestamps.
    Rule{ValidateMedianTimePast},
    // A header timestamp MUST be less than or equal to network-adjusted time plus 2 hours.
    Rule{ValidateTimestampCurrent},
    // A header version number MUST meet deployment requirements depending on activated BIPs.
    Rule{ValidateVersion}
  };
  HeaderValidationContext context{header, parent, view, parent.height + 1};
  return ValidateRules<HeaderError>(ruleset, 0, context);
}

// Performs transaction validation, aligned with Core's CheckTransaction function.
[[nodiscard]] inline auto ValidateTransaction(const protocol::TransactionConstView transaction) {
  static constexpr std::array ruleset = {
    // A transaction MUST contain at least one input.
    Rule{ValidateInputCount},
    // A transaction MUST contain at least one output.
    Rule{ValidateOutputCount},
    // A transaction's serialized size (excluding witness data) MUST NOT exceed 1,000,000 bytes.
    Rule{ValidateTransactionSize},
    // All output values MUST be non-negative, and their sum MUST NOT exceed 21,000,000 coins.
    Rule{ValidateOutputValues},
    // A transaction's inputs MUST reference distinct outpoints (no duplicates).
    Rule{ValidateUniqueInputs},
    // In a coinbase transaction, the scriptSig MUST be between 2 and 100 bytes inclusive.
    Rule{ValidateCoinbaseSignatureSize},
    // A non-coinbase transaction's inputs MUST have non-null prevout values.
    Rule{ValidateInputsPrevout}
  };
  return ValidateRules<TransactionError>(ruleset, 0, transaction);
}

// Performs non-contextual block validation, aligned with Core's CheckBlock function.
[[nodiscard]] inline auto ValidateBlockStructure(const protocol::Block& block) {
  static constexpr std::array ruleset = {
    // A block MUST contain at least one transaction.
    Rule{ValidateNonEmpty},
    // A block’s Merkle root field MUST equal the Merkle root of its transaction list.
    Rule{ValidateMerkleRoot},
    // A block’s serialized size (before SegWit) MUST NOT exceed 1,000,000 bytes.
    Rule{ValidateOriginalSizeLimit},
    // A block MUST contain exactly one coinbase transaction, and it MUST be the first transaction.
    Rule{ValidateCoinbase},
    // All transactions in a block MUST be valid according to transaction-level consensus rules.
    Rule{ValidateTransactions},
    // The total number of signature operations in a block MUST NOT exceed the consensus maximum.
    Rule{ValidateSignatureOps}
  };
  // clang-format on
  return ValidateRules<BlockOrTransactionError>(ruleset, 0, block);
}

// Performs contextual block validation, aligned with Core's ContextualCheckBlock function.
[[nodiscard]] inline auto ValidateBlockContext(const protocol::Block& block,
                                               const int height,
                                               const AncestorTimestampsView& ancestry) {
  static constexpr std::array ruleset = {
    // All transactions in the block MUST be final given the block height and locktime rules.
    Rule{ValidateTransactionFinality},
    // From BIP34, the coinbase transaction’s scriptSig MUST begin by pushing the block height.
    Rule{ValidateCoinbaseHeight,        BIP::HeightInCoinbase },
    // From BIP141, the coinbase transaction MUST include a valid witness commitment for blocks containing witness data.
    Rule{ValidateWitnessCommitment,     BIP::SegWit           },
    // A block’s total weight MUST NOT exceed 4,000,000 weight units.
    Rule{ValidateBlockWeight}
  };
  BlockValidationContext context{block, ancestry, ancestry.Length()};
  return ValidateRules<BlockError>(ruleset, context.height, context);
}
```
> **Figure 8.** *A complete, declarative, pure, and functioning specification for Bitcoin's header, transaction, and block validation rules in 50 lines of idiomatic modern C++.*

These compact C++ rulesets demonstrate that Bitcoin’s consensus can be expressed declaratively, without hidden state or side effects, and still execute at full performance on mainnet. Yet C++ remains a general-purpose language: it allows styles that drift from this discipline, and its semantics are not tailored to formal reasoning. To go further, we introduce Hornet DSL: a purpose-built language that encodes consensus rules unambiguously and enforces constraints by design.


## 5. Hornet DSL


### 5.1 Design goals

We have shown that Bitcoin's consensus rules can be expressed compactly and declaratively in C++. Hornet DSL takes this further: a purpose-built language that enforces purity, immutability, and composability at the syntax level, while providing natural expression with built-in knowledge of  Bitcoin concepts. 

Whereas C++ permits many styles, Hornet DSL constrains expression to constructs needed for pure consensus specification, making every rule's action bounded and semantically clear. The goal is an unambiguous, implementation-indepedent, compilable specification of Bitcoin consensus, suitable for automated testing, code generation, and formal verification. 

Even without full formal methods, Hornet DSL's constrained and regular structure enables more effective analysis and LLM-based reasoning, compared to the highly imperative and stylistically diverse reference client.

### 5.2 Language features

With these goals in mind, Hornet DSL is defined to have the following features:-
- Deterministic operation (functions executed with identical inputs yield identical outputs)
- All state must be local and explicit (no globals, statics, or member functions)
- All functions execute in a single-threaded context
- All variables are immutable unless explicitly marked mutable
- All functions must return a value and have no side effects
- Types are built-in or plain structs
- Protocol structs natively define their serialization format
- A validation `rule` is a function that returns success or a typed error
- Validation rules may be composed into a ruleset of ordered, identically typed rules
- Validation rules may be tagged with a `@bip` annotation for selective application depending on block height
- Validation rules are invoked with the `require` keyword, which passes the enclosing function's arguments to the referenced rule, returns early on error, or otherwise proceeds


### 5.3 Example: Contextual block validation

In this context, we present again the Hornet DSL snippet from Figure 1:

```C++
// Performs contextual block validation, aligned with Core's ContextualCheckBlock function.
@rule @phase("block_context") 
rule ValidateBlockContext(block: Block, 
                          height: uint32, 
                          past_timestamps: array<uint32, 11>)
                       -> BlockError? {

  // All transactions in the block MUST be final given the block height and locktime rules.
  require ValidateTransactionFinality
  // From BIP34, the coinbase transaction’s scriptSig MUST begin by pushing the block height.
  @bip(BIP34) require ValidateCoinbaseHeight
  // From BIP141, the coinbase transaction MUST include a valid witness commitment for blocks containing witness data.
  @bip(BIP141) require ValidateWitnessCommitment
  // A block’s total weight MUST NOT exceed 4,000,000 weight units.
  require ValidateBlockWeight
}
```
> **Figure 9.** Designing Hornet DSL for Bitcoin consensus specification.

### 5.4 Testing, Reasoning, and Guarantees

Hornet DSL is a work in progress, iteratively informed by Hornet Node's evolving declarative C++. Once complete, the DSL will be a human- and machine-readable precise specification. An early goal at that stage will be to generate C++ that matches Hornet Node's validation pipeline. We will then be able to set up cloud-scale automated testing that continuously validates Hornet DSL backends against Hornet Node and Bitcoin Core.

We believe that it becomes highly plausible to use LLM-based models (with access to source code and compiler) in a test framework to continuously analyze the Hornet DSL and C++ specifications, assess their agreement with Bitcoin Core behavior, and generate differential adversarial test cases in a feedback-driven loop. This includes both differential testing (comparing outputs across mutiple implementations) and adaptive test generation (where the results of one probe inform the construction of the next). 

Such targeted probing provides far more effective search of the high-dimensional space of all blocks than general fuzzing, leading to much stronger evidence that the specification indeed matches the Bitcoin reference client. After testing one billion blocks for potential edge case bugs, we will have simulated ~20,000 years of validated consensus behavior.

Of course, formal reasoning remains the goal for pure and mathematical proof. But this *semi-formal* approach may allow us to make quantifiable guarantees about consensus correctness. The space of all possible blocks is too vast to enumerate. Yet the space of all possible code paths in a structured specification like Hornet is very much smaller. With this approach, we believe we can reduce the probability of consensus bugs to an arbitrarily small value.

We now return to Hornet Node to describe its other key implementaiton contributions.

## 6. Implementation Details




<table> <tr> <th>Declarative C++</th> <th>Hornet DSL</th> </tr> <tr> <td>

```cpp
// Performs header validation, aligned with Core's CheckBlockHeader and ContextualCheckBlockHeader.
[[nodiscard]] inline auto ValidateHeader(const protocol::BlockHeader& header,
                                         const model::HeaderContext& parent,
                                         const AncestorTimestampsView& view) {
  const std::array ruleset = {
    // A header MUST reference the hash of its valid parent.
    Rule{ValidatePreviousHash},     
    // A header's 256-bit hash value MUST NOT exceed the header's proof-of-work target.
    Rule{ValidateProofOfWork},      
    // A header's proof-of-work target MUST satisfy the difficulty adjustment formula.
    Rule{ValidateDifficultyAdjustment},
    // A header timestamp MUST be strictly greater than the median of its 11 ancestors' timestamps.
    Rule{ValidateMedianTimePast},   
    // A header timestamp MUST be less than or equal to network-adjusted time plus 2 hours.
    Rule{ValidateTimestampCurrent}, 
    // A header version number MUST meet deployment requirements depending on activated BIPs.
    Rule{ValidateVersion}           
  };
  HeaderValidationContext context{header, parent, view, parent.height + 1};
  return ValidateRules<HeaderError>(ruleset, 0, context);
}
```

</td><td>

```cpp

// Performs header validation, aligned with Core's CheckBlockHeader and ContextualCheckBlockHeader.
@rule @phase("header") 
rule ValidateHeader(context : HeaderValidationContext) -> HeaderError? {
  // A header MUST reference the hash of its valid parent.
  require ValidatePreviousHash
  // A header's 256-bit hash value MUST NOT exceed the header's proof-of-work target.
  require ValidateProofOfWork
  // A header's proof-of-work target MUST satisfy the difficulty adjustment formula.
  require ValidateDifficultyAdjustment
  // A header timestamp MUST be strictly greater than the median of its 11 ancestors' timestamps.
  require ValidateMedianTimePast
  // A header timestamp MUST be less than or equal to network-adjusted time plus 2 hours.
  require ValidateTimestampCurrent
  // A header version number MUST meet deployment requirements depending on activated BIPs.
  require ValidateVersion
}
```

</td></tr></table> 

> **Figure 1.** *Where we are and where' we going: On the left is our executable declarative constrained C++ implementation of Bitcoin's header validation rules. This is working today and is used to sync and validate all mainnet headers in under 3 seconds. On the right is the work in progress to transform our declarative C++ into Hornet DSL, a domain-specific language designed to express Bitcoin's consensus rules, and enable reasoning and formal verification. See text for details.*



## 3. Specification

Then how to write C++ validation in a similar style: free functions, no mutations, all variables const, no side effects, single threaded, composable, declarative.



Then discuss more about Hornet Node as the client that implements these ideas: a ground-up, dependency-free, modern, elegant, and efficient C++ impl. Discuss Hornet Node's other main contributions: modular layered architecture, novel chaintree data structure, metadata sidecars. 

Results: list results for syncing headers and blocks to tip (not sure what metrics to quote). Give sync time excluding script validation and unspent validation at this stage (future work). Maybe list current rules as declarative spec or give examples. Screenshot of Hornet Node web UI synced to tip. 

Show C++ code examples for: Bitcoin script authoring and execution, self-contained script opcode handlers, polymorphic protocol message dispatch to subscribers, validation views to isolate consensus code from data structures, flat memory of blocks and VM stack without jagged arrays, interactive web UI for visualization. 

Future work. 

Summary. 

About the author.

## References
 
[1] NVK in *Bitcoin Fundamentals: BTC242 --- "Bitcoin Core vs Knots w/ NVK"* hosted by Preston Pysh. (*"Anything is possible, but it's ... completely unrealistic."*)
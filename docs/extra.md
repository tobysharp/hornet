

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

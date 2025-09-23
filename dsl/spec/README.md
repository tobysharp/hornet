# Hornet DSL

## **A language for Bitcoin consensus specification**

Hornet DSL is a declarative language for specifying Bitcoin consensus validation rules. Put simply, it specifies what properties must hold true of transactions, headers, and blocks, not how to compute them.

It is designed to read like mathematics, yet be efficiently executable.
Every construct in the language has a clear operational meaning, and every rule is written in a fail-fast style: evaluation halts at the first violated requirement.


---

## Design Principles

1. **Mathematical readability**

   * The language is designed to be read more than it is written. It is intended to enable a clear, concise, and expressive specification.
   * Rules are expressed in a compact style inspired by mathematics: inclusion (`∈`), quantifiers (`∀, ∃, ∃!`), summations (`Σ`), and case braces (`⎧ ⎨ ⎩`) are first-class syntax.
   * Field access and record updates resemble algebraic manipulation of tuples rather than imperative programming.

2. **Strong typing**

   * Every variable and field has an explicit type.
   * Bindings use the form `name ∈ Type`.
   * Tuples and sequences are typed compositions of other types.
   * Types may be implicit where type inferences allows them to be uniquely determined.

3. **Fail-fast semantics**

   * `Require` statements assert consensus conditions.
   * A failed `Require` aborts evaluation with the given label.
   * Sub-rules propagate failures upward.
   * A `Rule` is a function with no return value that contains at least one `Require` statement.
   * A `Require` statement may only be present inside a `Rule`.

---

## Notational Conventions

* **Tuples**

  * `(a ∈ T, b ∈ U)` is an ordered heterogeneous product of fields.
  * Fields may be named `(header ∈ Header, x ∈ bool)` or unnamed `(Header, bool)`.

* **Sequences**

  * `T⟨n⟩` = fixed-length sequence of `n` elements of type `T`.
  * `T⟨⟩` = variable-length sequence of elements of type `T`.
  * `⟨x, y, z⟩` = sequence literal.
  * `⟨ f(x) : x ∈ S ⟩` = sequence comprehension.
  * Indexing uses square brackets: `xs[i]`.

* **Sets**

  * `{a, b, c}` = finite set literal, used only when order and multiplicity do not matter (e.g. opcode membership).
  * In comprehensions, `{ f(x) : x ∈ S }` denotes a true set, not a sequence.

* **Quantifiers and Summations**

  * `∀ x ∈ S, P(x)` = for all `x` in `S`, predicate `P(x)` is true.
  * `∃ x ∈ S : P(x)` = there exists `x` in `S` such that `P(x)` is true.
  * `∃! x ∈ S : P(x)` = there exists exactly one `x` in `S` such that `P(x)` is true.
  * A separator (comma or colon) is mandatory; by convention, use `,` for `∀` and `:` for `∃`.
  * `Σ_{x ∈ S} f(x)` = summation over domain `S`. Domains may be sets or sequences; if a sequence, order and duplicates are preserved.

* **Case Expressions**

  * `⎧ expr1 if cond1`
    `⎨ expr2 if cond2`
    `⎩ expr3 otherwise`
  * Evaluated top-to-bottom; the first matching branch is taken.

* **Concatenation**

  * `s ⧺ t` = sequence concatenation.
  * `s ⧺^ n` = concatenation of `n` copies of `s`.
  * An element may be directly appended to a sequence if it is of the correct type.
  * ASCII shorthand: `#` may be used for `⧺` in plaintext.

* **Record Operations**

  * `r \ {f1, f2}` = record `r` with listed fields cleared.
  * `r with { f := v }` = record `r` with field `f` replaced by `v`.

---

## Examples

Here we present examples of the DSL code followed by their natural English reading.

```
Let money_supply ∈ int64 := 21,000,000 * 100,000,000  // satoshis
```

*Let `money_supply` be a 64-bit signed integer that is assigned the value twenty-one million multiplied by a hundred million (i.e. the total number of satoshis).*

```
Rule OutputValueRange(tx ∈ Transaction)
    Require ∀ out ∈ tx.outputs, out.value ∈ [0, money_supply]
```
*Define a rule `OutputValueRange` that applies to a `Transaction`, `tx`. Require that for every output `out` in `tx.outputs`, the value of `out` MUST be in the range from zero to `money_supply` inclusive. Otherwise, the current rule stack halts with error label `OutputValueRange`.*

```
Rule OutputValueSum(tx ∈ Transaction)
    Require Σ_{out ∈ tx.outputs} out.value ≤ money_supply
```
*Define a rule `OutputValueSum` that applies to a `Transaction`. We require that the sum of all its output values MUST be less than or equal to `money_supply`. Otherwise, the current rule stack halts with error label `OutputValueSum`.*

```
Rule ValidateTransaction(tx ∈ Transaction)
    Require OutputValueRange(tx)
    Require OutputValueSum(tx)
```
*Define a rule `ValidateTransaction` that applies to a `Transaction` and composes the two sub-rules, `OutputValueRange`, and `OutputValueSum`, each applied to the same transaction. If either rule fails, its error label is propagated.*

## More Examples

```
// The total number of signature operations in a block MUST NOT exceed the consensus maximum.
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
Define a rule `SigOpLimit` that applies to a `Block`:

Let `SigOpCost` be a function that maps each `OpCode` (`op`) to an integer cost according to three cases: 1 if `op` is `Op_CheckSig` or `Op_ChecksigVerify`; 20 if `op` is `Op_CheckMultiSig` or `Op_CheckMultiSigVerify`; and zero otherwise.

We then require that the total of all opcode costs, summed over every transaction in the block, every input and output script in those transactions, and every instruction in those scripts, MUST be less than or equal to 20,000. Otherwise, the rule fails and propagates error label `SigOpLimit`.

Note that `tx.inputs.scriptSig` is a projection with the meaning `⟨input.scriptSig : input ∈ inputs⟩`: the sequence of all sig scripts for each input in `inputs`.
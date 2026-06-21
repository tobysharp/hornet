# secp256k1 Performance History

This document is a running hand-off log for secp256k1 performance work in Hornet.

Its purpose is twofold:

1. Preserve the causal history of which changes moved which benchmarks.
2. Provide a stable place to record later optimizations against the same benchmark set.

The numbers below are single-threaded microbenchmark results collected during the secp256k1 optimization work on this branch. Treat them as trajectory markers rather than universal absolutes. The useful signal is the direction and the relative change between revisions measured under the same conditions.

## Canonical Benchmark Set

The current dedicated benchmark target is [bench/secp256k1_bench.cpp](../bench/secp256k1_bench.cpp).

The core microbenchmarks to track are:

- `BM_Secp256k1_VerifySignature` (the default path; now GLV — previously wNAF)
- `BM_Secp256k1_PointAdd`
- `BM_Secp256k1_PointMultiply`
- `BM_MultiplyModP_256_Secp256k1P`
- `BM_MultiplySelfModP_256_Secp256k1P`
- `BM_ReduceModuloP_256_Secp256k1P`
- `BM_SquareModP_256_Secp256k1P`
- `BM_InvertModuloOdd_256_Secp256k1P`

Companion benchmarks used for strategy comparison and kernel breakdown (not primary trend lines):

- `BM_Secp256k1_VerifySignature_JointNAF` — full verify with joint NAF, the comparison baseline for the default GLV verify
- `BM_Secp256k1_PointDouble`, `BM_Secp256k1_PointAddMixed` — point-formula breakdown (double; Jacobian + affine mixed add)
- `BM_LinComb_JointNAF`, `BM_LinComb_DisjointNAF`, `BM_LinComb_wNAF` — the bare `u1*G + u2*Q` linear combination per recoding strategy
- `BM_BigUint256_MultiplyWideSelf`, `BM_BigUint256_Squared` — wide-integer multiply/square below the field layer

These are the key command patterns to reuse when adding later entries:

```bash
cmake --build --preset clang20-release-all
build/clang20-release/secp256k1_bench
```

For a narrower run:

```bash
build/clang20-release/secp256k1_bench \
  --benchmark_filter='BM_Secp256k1_(VerifySignature|PointAdd|PointMultiply)|BM_(MultiplyModP|MultiplySelfModP|ReduceModuloP|SquareModP|InvertModuloOdd)_256_Secp256k1P' \
  --benchmark_min_time=1s
```

## Measurement Notes

- Use the release build.
- These numbers are intended to be compared only against other release measurements from the same machine class.
- The benchmark corpus was changed from fixed inputs to deterministic mixed corpora so that the reported throughput is more representative and less vulnerable to constant folding or pathological best cases.
- On 2026-06-19 the `VerifySignature` corpus was de-degenerated to random key pairs (previously the public key was the generator, so `Q = G`, which under-exercised the `Q` side of the linear combination). Corpus invariants are now enforced by a hard `BenchCheck` (aborts on failure) instead of `Assert`, which is a no-op under `NDEBUG`; the benchmark can no longer silently time an incorrect computation. Verify numbers from 2026-06-19 onward are therefore not directly comparable to earlier rows.
- Also on 2026-06-19 the default verify path switched from joint NAF to wNAF, so `BM_Secp256k1_VerifySignature` now measures the wNAF path; `BM_Secp256k1_VerifySignature_JointNAF` retains the joint-NAF number for comparison. The wNAF path amortises a fixed-base generator table built once outside the timed region, matching real verify where `G` is constant.
- On 2026-06-21 the default verify path switched again, from wNAF to **GLV** (lambda endomorphism), so `BM_Secp256k1_VerifySignature` now measures GLV. The point/field kernel benchmarks are unaffected — GLV changes only the scalar-multiplication structure. There is not yet a `BM_LinComb_GLV` companion, so GLV is tracked at the verify level only.
- Starting on 2026-05-30, the benchmark formerly labeled `ReduceModuloP(x, y)` was renamed to `Multiply mod p`, because that path performs a wide multiply and then reduction.
- `Multiply mod p` is the representative mixed-operand field-multiply benchmark.
- `Multiply self mod p` shares the same one-input field corpus as `Square mod p` and is the correct comparison baseline when evaluating whether squaring is cheaper than multiplication.
- `Reduce mod p` is a pure reducer benchmark over precomputed wide products; it is diagnostic rather than a direct end-to-end field operation.
- `VerifySignature` is not expected to track `PointAdd`. It contains scalar multiplication work and normalization costs, and historically was much closer to the scalar-multiply cost model than to point addition.
- After the fast reduction landed, field multiplication ceased to be the dominant end-to-end cost. Field inversion and point-formula structure remained important.
- After Jacobian points landed, point multiplication improved substantially because many inversions were removed from the inner loop.
- CPU governor and core placement materially affect the verify benchmark on this machine. A pinned-core run under the `powersave` governor measured about ~2.95k/s for `VerifySignature`, versus about ~3.15k/s on boosted unpinned runs from the same source state. Record governor and affinity whenever comparing close results.

## Progression Table

This is the single canonical progression table for this file. Append new entries here. Numbers are approximate where they were discussed qualitatively or rounded in bench output.

| When | Commit / branch note | Change | Verify sig | Point add | Point multiply | Multiply mod p | Multiply self mod p | Square mod p | Pure reduce mod p | Invert mod p | Notes |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| Early microbenchmark cut | n/a | Fixed-input focused benchmark added | ~135/s | ~118k/s | not yet tracked | ~626k/s | n/a | n/a | n/a | n/a | Early numbers; fixed inputs were later judged too synthetic |
| Mixed corpus baseline | n/a | Switched to deterministic mixed corpora | ~138/s | ~120.8k/s | ~278.2/s | ~699.8k/s | n/a | n/a | n/a | n/a | First directly useful baseline set |
| Pre-reduction comparison | n/a | Added dedicated reducer benchmark before major optimization | n/a | n/a | n/a | ~694k/s | n/a | n/a | ~703k/s | n/a | Specialized reducer existed but had not yet materially separated from the generic path |
| Fast reduction landed | n/a | Optimized `ReduceModuloP` for secp256k1 pseudo-Mersenne structure | n/a | n/a | n/a | old path ~707k/s equivalent | n/a | n/a | ~38.7M/s | n/a | Local reducer speedup on the order of 55x |
| Post-reduction snapshot | n/a | Fast reduction wired into field multiplication | ~346.6/s | ~274.4k/s | ~691.5/s | ~38.6M/s | n/a | n/a | n/a | ~311k/s | Historical row retained under `Multiply mod p`; the old benchmark label still said `ReduceModuloP` but this path already included multiplication |
| 2026-05-29 | `tsharp/feature/public-key-type` | `Curve::Point` moved to Jacobian representation with affine conversion boundary | ~2.10k/s | ~1.10M/s | ~3.58k/s | ~38.03M/s | n/a | n/a | n/a | ~305k/s | Full-suite Jacobian snapshot; `Multiply mod p` keeps the historical result from the pre-rename benchmark |
| 2026-05-29 | `tsharp/feature/public-key-type` | `VerifySignature` switched to shared multi-scalar accumulation for `u1 * G + u2 * Q` | ~3.18k/s | ~1.13M/s | ~3.68k/s | ~38.63M/s | n/a | n/a | n/a | ~295k/s | Filtered-suite Strauss-Shamir snapshot with `--benchmark_min_time=1s`; single run with CPU scaling warning present |
| 2026-05-30 | `tsharp/feature/secp256k1-opt` | Replaced field multiplies with squares and adds inside point addition | ~2.95k/s | n/a | n/a | n/a | n/a | n/a | n/a | n/a | Verify-only methodology cross-check on local worktree using `taskset -c 0` under governor `powersave` and AMD `balance_performance` |
| 2026-05-30 | `tsharp/feature/secp256k1-opt` | Switched `BigUint::MultiplyWide` to Comba accumulation | ~9.49k/s | ~3.92M/s | ~12.82k/s | ~81.69M/s | n/a | n/a | n/a | ~310k/s | Filtered suite run with `--benchmark_min_time=1s` from `build/clang20-release/secp256k1_bench`; governor `powersave`, affinity `0-31`, benchmark reported CPU scaling warning |
| 2026-05-30 | `tsharp/feature/secp256k1-opt` | `BigUint::Squared` kept generic `Unroll` structure and switched off-diagonal terms from duplicated accumulation to one doubled 128-bit add | ~9.78k/s | ~4.05M/s | ~13.28k/s | ~81.85M/s | n/a | n/a | n/a | ~311k/s | Filtered suite run with `--benchmark_min_time=1s` from `build/clang20-release/secp256k1_bench`; governor `powersave`, affinity `0-31`, benchmark reported CPU scaling warning |
| 2026-05-30 | `tsharp/feature/secp256k1-opt` | Renamed the old `ReduceModuloP(x, y)` benchmark to `Multiply mod p`, and added separate pure `ReduceModuloP(UIntW<512>)` and `Square mod p` benchmarks | n/a | n/a | n/a | ~81.69M/s | n/a | ~92.86M/s | ~159.95M/s | n/a | Exploratory mod-p-only run with `--benchmark_filter='BM_(MultiplyModP|ReduceModuloP|SquareModP)_256_Secp256k1P' --benchmark_min_time=0.2s`; retained for chronology, not the preferred ratio row |
| 2026-05-30 | `tsharp/feature/secp256k1-opt` | Standardized the mod-p microbenchmarks around representative mixed multiply, matched self-multiply vs square, and a diagnostic pure reducer | n/a | n/a | n/a | ~82.46M/s | ~82.98M/s | ~92.81M/s | ~160.13M/s | n/a | One-second mod-p run with `--benchmark_filter='BM_(MultiplyModP|MultiplySelfModP|ReduceModuloP|SquareModP)_256_Secp256k1P' --benchmark_min_time=1s`; shared one-input corpus for `Multiply self mod p`, `Square mod p`, and `Invert`; benchmark reported CPU scaling warning |
| 2026-05-31 | `tsharp/feature/secp256k1-opt` | Reworked verification linear combination onto NAF-based Strauss-Shamir with mixed affine/Jacobian dispatch and signed lookup tables | ~15.61k/s | n/a | n/a | n/a | n/a | n/a | n/a | n/a | Verify-only run with `build/clang20-release/secp256k1_bench --benchmark_filter=BM_Secp256k1_VerifySignature --benchmark_min_time=1s --benchmark_repetitions=5`; mean CPU time ~64.07 us, benchmark reported CPU scaling warning |
| 2026-06-19 | `tsharp/feature/secp256k1-opt` | Step 0 correctness/hygiene re-baseline: fixed the int8_t wNAF digit/accumulator overflow (now `int16_t`), restored joint NAF as the executed verify path, de-degenerated the verify corpus to random key pairs (was public key == G, i.e. Q = G), and made the corpus correctness check a hard gate outside the timed region | ~11.51k/s | n/a | n/a | n/a | n/a | n/a | n/a | n/a | **Honest re-baseline, not comparable to the 2026-05-31 ~15.61k/s row**: that number was measured on a degenerate Q = G corpus; random-key verify is ~11.5k/s. wNAF is kept bench-only behind a precomputed affine G-table (w=10) with a differential check vs joint NAF. Pinned `taskset -c 0`, governor `powersave`, EPP `balance_performance`, `--benchmark_min_time=1s --benchmark_repetitions=5`, cv 0.03%; CPU scaling warning present. Full-verify like-for-like (`BM_Secp256k1_VerifySignature` vs `BM_Secp256k1_VerifySignature_wNAF`, same random-key corpus, only the linear combination differs, wNAF G-table precomputed once outside timing as in real verify): joint NAF ~11.51k/s vs **wNAF ~14.05k/s (~22% faster end to end)**. Companion `u1*G + u2*Q` lincomb micro-benches: joint NAF ~13.07k/s, disjoint NAF ~12.39k/s, wNAF ~16.37k/s. **Not apples-to-apples for the lincomb row**: the joint/disjoint benches use two random bases and build their tables inside the timed region, while wNAF uses the verify-shaped fixed base G with its wide (w=10) table precomputed *outside* timing (only the per-call Q-table is timed). So the wNAF lead reflects the fixed-base verify scenario plus sparser wide-window adds, not a clean recoding-vs-recoding result; the confound-separated comparison is Step 3 |
| 2026-06-19 | `tsharp/feature/secp256k1-opt` | Wired wNAF as the **default** verify path (full canonical-suite snapshot). The fixed-base generator table is now a static `Curve` member with an explicit `BuildGeneratorTable(width)` plus a thread-safe (`call_once`) on-demand build at default width 10; `VerifySignature` uses wNAF, joint NAF stays available via `VerifySignatureWith` for comparison | ~14.03k/s | ~3.87M/s | ~15.22k/s | ~80.35M/s | ~81.06M/s | ~91.01M/s | ~156.6M/s | ~303.2k/s | Full canonical suite, single pinned run: `taskset -c 0`, governor `powersave`, EPP `balance_performance`, `--benchmark_min_time=1s --benchmark_repetitions=5`, cv ≤ 0.11%, CPU scaling warning present. Like-for-like full verify on the same random-key corpus: joint NAF `BM_Secp256k1_VerifySignature_JointNAF` ~11.49k/s vs default wNAF `BM_Secp256k1_VerifySignature` ~14.03k/s (~22% faster). Companion benches: point double ~7.51M/s, mixed (jac+aff) add ~5.07M/s; lincomb `u1*G + u2*Q` joint NAF ~13.01k/s, disjoint NAF ~12.38k/s, wNAF ~16.32k/s; `BigUint::MultiplyWide`(self) ~170.7M/s, `BigUint::Squared` ~173.6M/s. Default path exercised by all existing verify KAT / SigOps tests plus a new wNAF-vs-joint differential (100 random sigs) and a generator-table width sweep (w=2..12). Generator-table width tuning (suspected w≈8 cache peak) deferred to Step 3 |
| 2026-06-21 | `tsharp/feature/secp256k1-opt` | GLV / lambda endomorphism as the **default** verify path (Step 1). Decompose `u1`, `u2` into ~128-bit halves via the β/λ lattice and run a 4-term Strauss `u1a·G + u1b·φ(G) + u2a·Q + u2b·φ(Q)`, halving the shared doublings (~256 → ~128) while additions stay ~constant. `φ(G)` table built once next to the `G` table; `φ(Q)` derived per call by scaling X by β | ~18.24k/s | ~3.86M/s | ~15.24k/s | ~80.57M/s | ~81.33M/s | ~91.26M/s | ~157.59M/s | ~305.7k/s | Full canonical suite, single pinned run: `taskset -c 0`, governor `powersave`, EPP `balance_performance`, `--benchmark_min_time=1s --benchmark_repetitions=5`, cv ≤ 0.28%, CPU scaling warning present. `BM_Secp256k1_VerifySignature` now measures the **GLV** default; like-for-like over the same random-key corpus, GLV ~18.2k/s vs the prior wNAF default ~14.03k/s (**~+30%**) and vs `BM_Secp256k1_VerifySignature_JointNAF` ~11.50k/s (~1.58×). Point/field kernel columns are unchanged from the 2026-06-19 wNAF row — GLV restructures only the scalar ladder. Companion lincomb benches (two random bases, tables built in-region): joint NAF ~13.05k/s, wNAF ~16.38k/s; there is no GLV lincomb bench yet, so the GLV number is verify-level only. The decomposition still uses an exact bignum division (`round_div`) and generic `Fp<n>` reduction in the combine — both unoptimized (multiply-shift split and a fast `ReduceModuloN` are follow-ups). In-repo correctness: `SplitLambda` property test (`k ≡ k1 + k2·λ (mod n)`, `|k_i| < 2^128`), `LinearCombination_GLV`-vs-joint-NAF differential (200 random), plus the existing verify KAT / random-sig differential now running through GLV |

## Interpretation

There are now four clear phases in the performance history so far.

### 1. Measurement got more honest

The first benchmark cut was useful for rough orientation, but fixed inputs understated the amount of variation in the hot path. Moving to deterministic mixed corpora gave a more realistic baseline and made later comparisons more trustworthy.

### 2. Fast field reduction was a major local win

The secp256k1-specific `ReduceModuloP` optimization moved pure reduction from roughly the sub-mega-op/s range into the high tens of millions of ops per second. That eliminated generic reduction as the dominant cost in raw field multiplication.

After the later benchmark split, the current pure reducer microbenchmark is substantially faster again, at about ~160.13M/s on this machine, while `Multiply mod p` remains lower because it still includes wide multiplication.

The matched one-input measurements also show that `Square mod p` is genuinely cheaper than `Multiply self mod p` in the current implementation, rather than that gap being caused by benchmark harness asymmetry.

### 3. Projective/Jacobian formulas enabled the next major step

Once multiplication and reduction got much faster, inversions and affine-style formulas became the limiting factor. The later Jacobian point work removed many inversions from scalar multiplication and produced the next large jump, with that Jacobian snapshot showing point multiplication at about 3.58k ops/s and signature verification at about 2.10k ops/s.

### 4. Strauss-Shamir improved verification more than the core kernels

Once the point kernels were already in good shape, the next meaningful gain came from changing verification structure rather than raw field arithmetic. The Strauss-Shamir linear combination reuses doublings and mixed additions across `u1 * G + u2 * Q`, which pushed `VerifySignature` to about 3.18k ops/s while the standalone `PointMultiply` and `PointAdd` benchmarks moved only modestly. That pattern is exactly what this optimization should produce.

### 5. Honest re-baseline, then wNAF as the default verify

Two things happened on 2026-06-19. First, a correctness and measurement cleanup: a latent `int8_t` overflow in the wNAF recoder was fixed (digits are now `int16_t`), the verify corpus was de-degenerated from `Q = G` to random key pairs, and the bench's correctness check became a hard gate that also fires under `NDEBUG`. That moved the *reported* verify from the old degenerate ~15.61k/s down to an honest ~11.5k/s on joint NAF — not a regression, just a representative measurement.

Second, wNAF became the default verify path. Over the same random-key corpus it runs ~14.0k/s versus ~11.5k/s for joint NAF (~22% faster end to end), by amortising a wide fixed-base table for `G` and recoding both scalars more sparsely so the additions roughly halve. With additions cut, the verify cost is now dominated by the ~256 shared point doublings and the two field inversions in the tail (`s^-1 mod n` and the R normalization) — which is exactly where the next steps aim: GLV to halve the doublings, and projective x-comparison plus batching to remove or amortise the inversions.

### 6. GLV halved the doublings

On 2026-06-21 the lambda endomorphism (GLV) became the default verify. Decomposing each scalar into two ~128-bit halves and running a 4-term Strauss over `G`, `φ(G)`, `Q`, `φ(Q)` halves the shared doublings (~256 → ~128) while the total additions stay roughly constant (twice as many terms, each half-length). That moved verify from ~14.0k/s (wNAF) to ~18.2k/s (~+30%) with the point/field kernels untouched — the gain is purely ladder structure, exactly as the "additions cut, doublings dominate" read in §5 predicted.

The GLV path is a deliberate first pass; the remaining GLV-specific levers, none yet applied, are:

- **Multiply-shift split.** `SplitLambda`'s `round(b·k/n)` uses an exact bignum `QuotientRemainder` per term; replace with a precomputed multiply-shift ($g_i \approx \lfloor 2^t|b_i|/n\rceil$, then $\beta_i = (k g_i)\gg t$) as libsecp does.
- **Fast `ReduceModuloN`.** The mod-`n` combine (and the existing `u1,u2,s⁻¹`) falls through `Fp<n>` to generic long division — `n` is not pseudo-Mersenne but $c_n = 2^{256}-n = 2^{128}+d$ admits a libsecp-style fold (or Barrett/Montgomery).
- **Globalz affine `Q` table (Step 3).** The Q-side tables are Jacobian, so the Q adds are jac+jac; an inversion-free affine table makes them mixed adds.
- **`G`/`Q` window-width sweep.** `kQWidth=5` and the G width are sized for 256-bit scalars; the ~128-bit GLV regime likely wants narrower windows.

The "both-signs vs positive-only" table-storage question is already settled — see *Investigated and Set Aside*. Beyond GLV, the projective x-compare and batching remove the tail inversions.

## What Still Matters Most

When recording future entries, pay particular attention to which of these buckets the change attacks:

- Field reduction speed.
- Field inversion speed.
- Point addition / doubling formulas.
- Scalar multiplication strategy.
- Normalization boundaries (`operator Affine()` versus `NormalizedX()`).
- Verification structure beyond pure point arithmetic.

In practical terms, the ratios to watch are:

- `VerifySignature / PointMultiply`
- `PointMultiply / PointAdd`
- `InvertModuloOdd / Multiply mod p`
- `Square mod p / Multiply self mod p`

Those ratios help show whether the next bottleneck is inversion, point formulas, or verification overhead outside the scalar-multiply core.

## Investigated and Set Aside

Negative or marginal results worth recording so they are not re-investigated.

- **Positive-only wNAF G table (2026-06-19).** Storing only the positive odd multiples `{ P, 3P, 5P, ... }` (half the both-signs table) and recovering negative multiples with a branchless conditional negate (compute `-y = p - y`, `cmov` on the digit sign). Measured at full verify, pinned (`taskset -c 0`, `powersave`, reps=10, cv ≤ 0.04%) against the ~14.02k/s both-signs w=10 default:
  - half memory, same width (w=10, 16 KiB): ~14.00k/s, **−0.16%**. The cache hypothesis did not hold — at w=10 each verify touches only ~23 of 512 entries and 32 KiB already fits L1D (48 KiB), so halving buys no measurable cache win while the conditional negate costs a hair.
  - same memory, wider window (w=11, 32 KiB): ~14.07k/s, **+0.40%**. Real but marginal — the ~256 shared doublings dominate and the G-side adds are already sparse.
  - positive-only Q as well (w=11): ~14.06k/s, neutral-to-slightly-negative vs positive-G-only, confirming the small variable-base Q table (≈1 KiB, L1-resident) sees no cache upside.
  Conclusion: not worth the extra kernel path and per-add conditional-negate complexity; both-signs stays the default. (`std::copysign` is not applicable here — field negation is the modular subtract `p - y`, not an IEEE sign-bit flip.) Revisit only if a width sweep (w ≥ 12, table spilling L1) shows the G table going cache-bound — part of the Step 3 cache-knee work.

## Updating It

Append one new row to the progression table above for each meaningful optimization pass.

Use the notes column to capture things that would otherwise be forgotten later, for example:

- benchmark corpus changed
- point representation changed
- arithmetic identity changed but API did not
- result is provisional and from a single run
- result excludes or includes SEC1 parsing

## Suggested Logging Discipline

To keep this file useful, prefer recording:

- one row per optimization idea
- the benchmark command used
- whether the result is from a clean release rebuild
- any reason the result is not directly comparable to earlier rows

Do not silently replace older numbers. If methodology changes, add a new row and say so explicitly.
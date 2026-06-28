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
- `BM_LinComb_JointNAF`, `BM_LinComb_DisjointNAF`, `BM_LinComb_wNAF`, `BM_LinComb_GLV` — the bare `u1*G + u2*Q` linear combination per recoding strategy (`BM_LinComb_GLV` mirrors the verify combiner: per-call `SplitLambda` of both scalars + a Q/φ(Q) table + the 4-term Strauss over the fixed G/φ(G) tables, which are precomputed outside the timed region as in real verify)
- `BM_GLV_SplitLambda` — isolates the GLV scalar decomposition `k → (k1, k2)` with `k ≡ k1 + k2·λ (mod n)`, `|k_i| < 2^128`; the target of the multiply-shift split and fast `ReduceModuloN` follow-ups
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
- On 2026-06-27 the `BM_LinComb_GLV` and `BM_GLV_SplitLambda` companion benches were added, closing the "no GLV lincomb companion yet" caveat recorded for the 2026-06-21 GLV row (and superseding that note). This is a bench-only addition — no change to the default `VerifySignature` path, so no new progression row. Pinned run (`taskset -c 0`, governor `powersave`, EPP `balance_performance`, `--benchmark_min_time=1s --benchmark_repetitions=5`, cv ≤ 0.34%, CPU scaling warning present): `BM_LinComb_GLV` ~22.69k/s vs `BM_LinComb_wNAF` ~16.47k/s (**~1.38×**) vs `BM_LinComb_JointNAF` ~13.13k/s (~1.73×); `BM_GLV_SplitLambda` ~689k/s (~1.45 µs/split). The clean apples-to-apples pair is GLV vs wNAF — both use the fixed-base G table precomputed outside timing with only the per-call Q table timed, differing only in the λ split plus the half-length 4-term ladder; the joint/disjoint lincomb benches use two random in-region bases, so GLV-vs-joint at the lincomb level still carries the fixed-base confound flagged for 2026-06-19. The lincomb GLV/wNAF ~1.38× is larger than the verify-level GLV/wNAF ~+30% (2026-06-21 row) because full verify dilutes the ladder gain with shared per-verify overhead (`s⁻¹ mod n`, hashing, the projective compare). Split share: the two `SplitLambda` calls (~2.90 µs) are ~6.6% of the ~44.1 µs GLV lincomb — the slice the multiply-shift split and fast `ReduceModuloN` follow-ups (§6) target, now directly measurable.
- On 2026-06-27 a latent extra field multiply was removed from the **mixed (Jacobian + affine) point addition**. Both `operator+(Affine, Jacobian)` and the new `AddWithZRatio` formed `Z₃ = H·((Z+1)² − 1 − Z²)` — a squaring **and** a multiply to produce `2HZ`. Replaced with the standard identity `Z₃ = (Z+H)² − Z² − H²`, which reuses the already-computed `Z²` and `H²` for the same `2HZ` at one squaring and **no** multiply. This brings the kernel to its long-claimed **7M+4S** — it was really 8M+4S; the `// Add: 7M, 4S` comment had under-counted the `Z₃` multiply. Pinned (`taskset -c 0`, powersave, EPP `balance_performance`, `--benchmark_min_time=1s --benchmark_repetitions=5`, cv ≤ 0.38%): `BM_Secp256k1_PointAddMixed` **~5.07M/s → ~5.53M/s (+9%)**, matching one-fewer-mult out of ~7M+4S. End-to-end `BM_Secp256k1_VerifySignature` moved only slightly (~19.58k → ~19.64k/s, +0.3%): today only the GLV G-side adds are mixed (the Q/φ(Q) tables are still Jacobian → jac+jac), so the fix touches ~⅓ of the per-verify adds, and op-count predicts ~+0.6% (≈24 saved G-side mults). That +0.3% is at the cross-run noise floor, though — the same run shows `Invert mod p` ~295k/s vs ~304k/s on 2026-06-22 with no inversion change, so cross-run wobble is ≳1%. So the cleanly attributable signal is `BM_Secp256k1_PointAddMixed` (+9%); the verify delta should be pinned with a same-session A/B (temporarily reverting the `Z₃` lines) before being attributed. Jacobian+Jacobian add, point double, point multiply, and all field kernels are otherwise unchanged — the fix is confined to the mixed-add `Z₃`; the jac+jac `Z₃ = ((Z₁+Z₂)²−Z₁²−Z₂²)·H` genuinely needs its multiply and was left alone. Recorded as the 2026-06-27 progression row below.
- On 2026-06-27 (follow-up to the globalz row) globalz was extended to the bench-only wNAF comparison path (`LinearCombination_wNAF`), so the GLV-vs-wNAF lincomb comparison is now **both-globalz** (apples-to-apples). Pinned (same protocol, cv ≤ 0.05%): `BM_LinComb_wNAF` **~16.49k → ~16.94k/s (+2.7%)** (its Q-side adds now mixed), `BM_LinComb_GLV` ~23.73k/s (unchanged within noise), `BM_LinComb_JointNAF` ~13.15k/s (control, no globalz). With both paths on globalz, **GLV still leads wNAF ~1.40×** at the lincomb (was ~1.44× when only GLV had globalz) — globalz hands both the same Q-side saving and leaves GLV's halved-doubling advantage intact, so GLV remains the faster default; no flip. NOTE: the `BM_LinComb_wNAF ~16.49k` cited as a "flat control" in the 2026-06-27 globalz row (and §8) was a point-in-time snapshot from *before* wNAF itself got globalz.

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
| 2026-06-22 | `tsharp/feature/secp256k1-opt` | Projective x-coordinate comparison in verify (Step 2). Replaces the final `R.NormalizedX()` (which inverts `Z`) with `IsJacobianXEqual(R.X, R.Z, r)`, testing `r·Z² ≡ X (mod p)` plus a `(r+n)·Z² ≡ X` branch gated on `r + n < p`, removing the one mod-p inversion in the verify tail | ~19.58k/s | ~3.84M/s | ~15.33k/s | ~80.48M/s | ~81.20M/s | ~91.20M/s | ~157.79M/s | ~303.6k/s | Full canonical suite, single pinned run: `taskset -c 0`, governor `powersave`, EPP `balance_performance`, `--benchmark_min_time=1s --benchmark_repetitions=5`, cv ≤ 0.77% (verify cv 0.03%), CPU scaling warning present. **Controlled A/B isolating Step 2** (same tree, only the final compare reverted to `NormalizedX().x.Modulo(n) == r.x`, identical filter/pinning): GLV verify **18.30k/s → 19.51k/s (+6.6%)**, `BM_Secp256k1_VerifySignature_JointNAF` 11.59k/s → 12.08k/s (+4.2%). The 18.30k `NormalizedX` baseline reproduces the logged 2026-06-21 GLV row (18.24k), confirming conditions match. The gain equals the removed inversion: `BM_InvertModuloOdd` ~303.6k/s ≈ 3.3 µs, ~6% of the ~54.6 µs old verify; the replacement field multiplies (one `Z²` + one or two products) are in the noise. Point/field kernel columns unchanged from the 2026-06-21 GLV row — Step 2 restructures only the verify tail. Verify field-inversion budget now **1** (`s⁻¹ mod n` only); the mod-p inversion is gone. New in-repo net `IsJacobianXEqualTest` (reduce_test.cpp): affine-reference + Jacobian-rescaling invariance over random inputs, the `t ≥ n` mod-n wrap branch (constructed — random points reach it only at ~2⁻¹²⁸), and the `r ≥ p − n` guard (wrong-accept + `r + n` overflow). All verify KAT / random-sig differentials now run through the projective compare; 461 hornetlib tests pass |
| 2026-06-27 | `tsharp/feature/secp256k1-opt` | Removed a redundant field multiply from the mixed (Jacobian + affine) point addition: compute `Z₃ = (Z+H)² − Z² − H²` (reusing the already-formed `Z²`, `H²`) instead of `H·((Z+1)² − 1 − Z²)`, making the mixed add a true **7M+4S** (it was 8M+4S — the cost comment had under-counted the `Z₃` multiply). Same fix in `operator+(Affine, Jacobian)` and the new `AddWithZRatio` | ~19.64k/s | ~3.90M/s | ~15.32k/s | ~80.25M/s | ~81.17M/s | ~91.10M/s | ~157.63M/s | ~295.4k/s | Full canonical suite, single pinned run: `taskset -c 0`, governor `powersave`, EPP `balance_performance`, `--benchmark_min_time=1s --benchmark_repetitions=5`, cv ≤ 0.38%, CPU scaling warning present. The directly-attributable win is the mixed-add kernel: companion `BM_Secp256k1_PointAddMixed` **~5.07M/s → ~5.53M/s (+9%)**, matching one-fewer-mult of 7M+4S. Verify is only ~+0.3% (19.58k→19.64k): today just the GLV G-side adds are mixed (Q/φ(Q) tables still Jacobian), ~⅓ of per-verify adds; op-count predicts ~+0.6% (~24 saved G-side mults), but the cross-run noise band is ≳1% here (same run: `Invert mod p` ~295k vs ~304k on 2026-06-22 with no inversion change), so the verify delta is at the noise floor and not cleanly attributable without a same-session A/B. Point/field kernels otherwise unchanged — the fix is confined to the mixed-add `Z₃`; the jac+jac `Z₃ = ((Z₁+Z₂)²−Z₁²−Z₂²)·H` needs its multiply and was left alone. 461/461 hornetlib tests pass. Payoff compounds once the globalz Q-table makes the Q/φ(Q) adds mixed |
| 2026-06-27 | `tsharp/feature/secp256k1-opt` | Globalz inversion-free affine Q/φ(Q) table (Step 3). The per-verify variable-base tables are now built in shared-Z affine form (`PrecomputeTableGlobalZ`: an `AddWithZRatio` z-ratio-telescoping chain on the C-scaled curve E_C + a multiply-only backward rescale), so the Q-side ladder adds drop from jac+jac (11M+5S) to mixed (7M+4S). The fixed G/φ(G) tables stay true-affine on E and are scaled into this verify's E_z frame on demand (`×g²,g³`); one final `Z·=g` maps the result back to E | ~20.45k/s | ~3.88M/s | ~15.34k/s | ~82.45M/s | ~83.01M/s | ~91.33M/s | ~157.69M/s | ~300.9k/s | Full canonical suite, pinned: `taskset -c 0`, governor `powersave`, EPP `balance_performance`, `--benchmark_min_time=1s --benchmark_repetitions=5`, cv ≤ 0.07%, CPU scaling warning present. **Clean same-session A/B vs the 2026-06-27 mixed-add row**: `BM_Secp256k1_VerifySignature` **19.64k → 20.45k/s (+4.1%)**, `BM_LinComb_GLV` **22.72k → 23.81k/s (+4.8%)**, while the non-globalz controls held flat (`VerifySignature_JointNAF` ~12.16k, `BM_LinComb_wNAF` ~16.49k, `PointAddMixed` ~5.52M) — confirming attribution. Magnitude matches the op-count: ~43 Q-side adds × (jac+jac − mixed ≈ 4M+1S) ≈ 210 M-equiv saved, minus ~23 G-side on-demand `g²,g³` scalings ≈ 46 M-equiv ⇒ ≈ +150 of ~3900 M-equiv/verify ≈ +4%; the ~128 shared doublings plus `s⁻¹ mod n` and 2× `SplitLambda` overhead dominate the rest (doublings still bind, per §5/§6). Point add (jac+jac), point multiply, double, and field kernels unchanged — globalz restructures only the variable-base tables + ladder frame; verify field-inversion budget still **1** (globalz is inversion-free). New in-repo net: `PrecomputeTableGlobalZMatchesAffineTableAcrossWidths` + `...AnchorsToKnownGeneratorMultiples` (curve_test.cpp), and the existing GLV-vs-jointNAF differential + verify KATs now run through globalz; 463/463 tests pass |
| 2026-06-28 | `tsharp/feature/secp256k1-opt` | Step 3a generator-table width tuning: raised the default fixed-base `G`/`φ(G)` wNAF window from **w=10 to w=12** (`BuildGeneratorTable(int width = 12)`), after a bench-only throughput sweep over `GW ∈ {7..15} × QW ∈ {4,5,6}`. The per-call variable-base `Q` width stays **5** (confirmed optimal). One-line src change; the GLV ladder infers its window from the table size, so nothing else moved | ~20.80k/s | ~3.88M/s\* | ~15.34k/s\* | ~82.45M/s\* | ~83.01M/s\* | ~91.33M/s\* | ~157.69M/s\* | ~300.9k/s\* | Pinned `taskset -c 0`, governor `powersave`, EPP `balance_performance`, `--benchmark_min_time=1s --benchmark_repetitions=5`, verify cv ≤ 0.03%, CPU scaling warning present. **Sweep was bench-only** (no src change to measure): each cell ran the production verify via `VerifySignatureWith` over locally-built width-`GW` `G`/`φ(G)` tables + `MakeVariableGlvTerm<QW>`; the `GW=10,QW=5` cell reproduced `BM_Secp256k1_VerifySignature` within noise (20.34k vs 20.36k). **Two findings, both refuting prior hypotheses:** (1) **QW=5 is optimal at every GW** (Q4 ≈ −1.2%, Q6 ≈ −2%) — separable, so the "128-bit GLV regime wants a narrower Q" guess is wrong. (2) **Verify throughput rises monotonically with GW through w=15 — no L1 cache knee** (the suspected w≈8 peak is wrong): each verify touches only ~17 of up to 2¹⁴ entries, so the fewer-G-adds win keeps beating cache cost. Returns decelerate (10→11 +1.0%, 11→12 +1.3%, 12→13 +0.6%, 13→14 +0.8%, 14→15 +0.4%) while the combined `G+φ(G)` footprint **doubles each step** (w=12 = 256 KiB, w=14 = 1 MiB = full L2). **Clean same-session A/B w=10→w=12: `BM_Secp256k1_VerifySignature` 20.28k → 20.76k/s (+2.4%)**; companions `BM_LinComb_GLV` 23.71→24.25k, `BM_LinComb_wNAF` 17.21k; control `BM_Secp256k1_VerifySignature_JointNAF` flat ~12.11k. **Chose w=12, not the microbench argmax** (still rising at w=15): the single-thread, no-contention bench understates per-core cache cost. The table is a `static` shared read-only copy — one L3 copy across all verify threads — but each core caches its touched lines in its private **1 MiB L2 (shared with its SMT sibling and any co-scheduled SHA256)**; 256 KiB stays L2-resident with headroom, whereas w≥14 (≥1 MiB) would fill L2 and evict the SHA256 / second-thread / future-batch working set. **Final width is to be re-tuned jointly with batch depth N at Step 4** — a wider `G` amortizes over a batch (and Step 4's pre-scaling removes the on-demand `g²,g³` premium) but trades against the L2-bound N. **\*Point/field kernel columns are carried forward from the 2026-06-27 globalz row** — width tuning touches only the verify ladder's `G`-table, not the point formulas or field reduction; a same-session spot-check reproduced them within run-to-run clock variance (the point kernels swing ≈±5–10% under EPP `balance_performance`), so they are not overwritten with today's noisier reads. 463/463 hornetlib tests pass |

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
- **Globalz affine `Q` table (Step 3).** ~~The Q-side tables are Jacobian, so the Q adds are jac+jac; an inversion-free affine table makes them mixed adds.~~ **Done 2026-06-27 (+4.1% verify) — see §8.**
- **`G`/`Q` window-width sweep.** `kQWidth=5` and the G width are sized for 256-bit scalars; the ~128-bit GLV regime likely wants narrower windows.

The "both-signs vs positive-only" table-storage question is already settled — see *Investigated and Set Aside*. Beyond GLV, the projective x-compare and batching remove the tail inversions.

### 7. Projective x-comparison removed the verify-tail inversion

On 2026-06-22 the final coordinate check in verify stopped normalizing `R`. Instead of computing the affine x via `R.NormalizedX()` (a `Z` inversion) and reducing it mod `n`, verify now keeps `R` Jacobian and tests `r·Z² ≡ X (mod p)` directly, with a second `(r+n)·Z² ≡ X` comparison gated on `r + n < p` to catch the case where the true affine x is `≥ n` (probability ~2⁻¹²⁸, since `n < p < 2n`). That removes the one mod-p inversion in the verify tail.

Under a controlled A/B that changed only the final comparison, GLV verify went 18.30k/s → 19.51k/s (+6.6%) and joint-NAF verify 11.59k/s → 12.08k/s (+4.2%). The win is small and exactly accounted for: the binary-GCD inversion microbenchmark is ~303.6k/s (~3.3 µs), about 6% of the ~54.6 µs old GLV verify — so removing it is essentially the whole gain, with the handful of replacement multiplies lost in the noise. The point and field kernel benchmarks are unchanged, as expected for a change confined to the verify tail.

This takes the verify field-inversion count from 2 to 1: only the scalar `s⁻¹ mod n` remains (still needed for the GLV split and to form `u1, u2`). The remaining lever on the inversion side is cross-verify batching (Step 4), which amortises that one scalar inversion across a batch via Montgomery's trick.

### 8. Globalz removed the Q-side jac+jac tax

On 2026-06-27 the per-verify variable-base tables (`Q` and `φ(Q)`) switched from Jacobian to **globalz** form — one shared `Z` across the whole table, built inversion-free by an `AddWithZRatio` z-ratio-telescoping chain on the C-scaled curve `E_C` plus a multiply-only backward rescale (`PrecomputeTableGlobalZ`). Feeding the entries as affine makes the Q-side ladder adds mixed (7M+4S) instead of jac+jac (11M+5S). The fixed `G`/`φ(G)` tables stay true-affine on `E` and are scaled into this verify's `E_z` frame on demand (`×g²,g³` per touched entry, cheaper than pre-scaling the wide fixed table); one final `Z·=g` maps the accumulator back to `E`.

This moved verify ~19.64k → ~20.45k/s (+4.1%) under a clean same-session A/B — the non-globalz controls (joint-NAF verify, wNAF lincomb, mixed add) held flat, isolating the gain. The size is exactly the Q-side add saving net of the G-side scaling premium (~+150 of ~3900 M-equiv/verify), and it is "only" ~4% because the ~128 shared doublings and the `s⁻¹`/split overhead dominate — the same "adds cut, doublings bind" pattern as §5/§6. With Step 2's projective compare, the verify is now field-inversion-free in both the ladder and the table build, leaving one scalar `s⁻¹ mod n`. The remaining §6 GLV levers — multiply-shift `SplitLambda` and fast `ReduceModuloN` — are still open; the G/Q width sweep is now done (§9).

### 9. Generator-table width: monotone, not a knee

On 2026-06-28 the Step 3a width sweep tuned the two wNAF window widths. The plan had assumed an interior cache knee (a suspected `G`-table peak near w≈8, where the table fits L1D, and a possibly-narrower `Q` for the ~128-bit GLV scalars). The sweep — bench-only, driving the real verify through `VerifySignatureWith` over locally-built width-`GW` tables — refuted both:

- **`Q` width is already optimal at 5** and is separable from `GW` (Q4 and Q6 lose ~1–2% at every `GW`). The narrow variable-base table (~1 KiB) was never the bottleneck.
- **`G` width has no knee in [7,15]** — verify throughput climbs monotonically with width, because the ladder touches only ~17 of up to 2¹⁴ table entries per verify, so the access is far too sparse for whole-table cache residency to bind. What actually improves is the add count: a wider window means fewer nonzero wNAF digits, hence fewer `G`-side adds. Returns decelerate while footprint doubles per step.

Because the curve has no interior optimum, picking a width is a footprint judgment, not an argmax. The default moved to **w=12** (256 KiB combined, +2.4% verify same-session) rather than the still-rising w=15: the single-thread microbench has no competing working set and so understates the real cost of a wide table. The `static` table is one shared L3 copy across all verify threads, but it occupies each core's **private 1 MiB L2** — shared with the SMT sibling and any co-scheduled SHA256 — so a 256 KiB table leaves headroom while w≥14 (≥1 MiB) would fill L2. The genuinely aggressive width is deferred to Step 4: batching amortizes a wide table over N sigs and pre-scaling removes the on-demand `g²,g³` premium, but the table footprint trades directly against the L2-bound batch depth N, so width and N must be tuned together. This is the same "adds cut, doublings bind" story as §5/§6 — width chips at the already-cheap `G`-side adds, so the ceiling is small until batching changes the structure.

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
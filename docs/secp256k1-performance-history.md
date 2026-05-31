# secp256k1 Performance History

This document is a running hand-off log for secp256k1 performance work in Hornet.

Its purpose is twofold:

1. Preserve the causal history of which changes moved which benchmarks.
2. Provide a stable place to record later optimizations against the same benchmark set.

The numbers below are single-threaded microbenchmark results collected during the secp256k1 optimization work on this branch. Treat them as trajectory markers rather than universal absolutes. The useful signal is the direction and the relative change between revisions measured under the same conditions.

## Canonical Benchmark Set

The current dedicated benchmark target is [bench/secp256k1_bench.cpp](../bench/secp256k1_bench.cpp).

The core microbenchmarks to track are:

- `BM_Secp256k1_VerifySignature`
- `BM_Secp256k1_PointAdd`
- `BM_Secp256k1_PointMultiply`
- `BM_MultiplyModuloM_256_Secp256k1P`
- `BM_ReduceModuloP_256_Secp256k1P`
- `BM_InvertModuloOdd_256_Secp256k1P`

These are the key command patterns to reuse when adding later entries:

```bash
cmake --build --preset clang20-release-all
build/clang20-release/secp256k1_bench
```

For a narrower run:

```bash
build/clang20-release/secp256k1_bench \
  --benchmark_filter='BM_Secp256k1_(VerifySignature|PointAdd|PointMultiply)|BM_(MultiplyModuloM|ReduceModuloP|InvertModuloOdd)_256_Secp256k1P' \
  --benchmark_min_time=1s
```

## Measurement Notes

- Use the release build.
- These numbers are intended to be compared only against other release measurements from the same machine class.
- The benchmark corpus was changed from fixed inputs to deterministic mixed corpora so that the reported throughput is more representative and less vulnerable to constant folding or pathological best cases.
- `VerifySignature` is not expected to track `PointAdd`. It contains scalar multiplication work and normalization costs, and historically was much closer to the scalar-multiply cost model than to point addition.
- After the fast reduction landed, field multiplication ceased to be the dominant end-to-end cost. Field inversion and point-formula structure remained important.
- After Jacobian points landed, point multiplication improved substantially because many inversions were removed from the inner loop.
- CPU governor and core placement materially affect the verify benchmark on this machine. A pinned-core run under the `powersave` governor measured about ~2.95k/s for `VerifySignature`, versus about ~3.15k/s on boosted unpinned runs from the same source state. Record governor and affinity whenever comparing close results.

## Progression Table

This is the single canonical progression table for this file. Append new entries here. Numbers are approximate where they were discussed qualitatively or rounded in bench output.

| When | Commit / branch note | Change | Verify sig | Point add | Point multiply | Multiply mod p | Reduce mod p | Invert mod p | Notes |
|---|---|---|---:|---:|---:|---:|---:|---:|---|
| Early microbenchmark cut | n/a | Fixed-input focused benchmark added | ~135/s | ~118k/s | not yet tracked | ~626k/s | n/a | n/a | Early numbers; fixed inputs were later judged too synthetic |
| Mixed corpus baseline | n/a | Switched to deterministic mixed corpora | ~138/s | ~120.8k/s | ~278.2/s | ~699.8k/s | n/a | n/a | First directly useful baseline set |
| Pre-reduction comparison | n/a | Added dedicated reducer benchmark before major optimization | n/a | n/a | n/a | ~694k/s | ~703k/s | n/a | Specialized reducer existed but had not yet materially separated from the generic path |
| Fast reduction landed | n/a | Optimized `ReduceModuloP` for secp256k1 pseudo-Mersenne structure | n/a | n/a | n/a | old path ~707k/s equivalent | ~38.7M/s | n/a | Local reducer speedup on the order of 55x |
| Post-reduction snapshot | n/a | Fast reduction wired into field multiplication | ~346.6/s | ~274.4k/s | ~691.5/s | ~38.6M/s | ~38.6M/s | ~311k/s | End-to-end speedup was real but bounded by inversion and point formulas |
| 2026-05-29 | `tsharp/feature/public-key-type` | `Curve::Point` moved to Jacobian representation with affine conversion boundary | ~2.10k/s | ~1.10M/s | ~3.58k/s | ~38.03M/s | ~38.06M/s | ~305k/s | Full-suite Jacobian snapshot |
| 2026-05-29 | `tsharp/feature/public-key-type` | `VerifySignature` switched to shared multi-scalar accumulation for `u1 * G + u2 * Q` | ~3.18k/s | ~1.13M/s | ~3.68k/s | ~38.63M/s | ~38.73M/s | ~295k/s | Filtered-suite Strauss-Shamir snapshot with `--benchmark_min_time=1s`; single run with CPU scaling warning present |
| 2026-05-30 | `tsharp/feature/secp256k1-opt` | Replaced field multiplies with squares and adds inside point addition | ~2.95k/s | n/a | n/a | n/a | n/a | n/a | Verify-only methodology cross-check on local worktree using `taskset -c 0` under governor `powersave` and AMD `balance_performance` |
| 2026-05-30 | `tsharp/feature/secp256k1-opt` | Switched `BigUint::MultiplyWide` to Comba accumulation | ~9.49k/s | ~3.92M/s | ~12.82k/s | ~81.69M/s | ~81.62M/s | ~310k/s | Filtered suite run with `--benchmark_min_time=1s` from `build/clang20-release/secp256k1_bench`; governor `powersave`, affinity `0-31`, benchmark reported CPU scaling warning |
| 2026-05-30 | `tsharp/feature/secp256k1-opt` | `BigUint::Squared` kept generic `Unroll` structure and switched off-diagonal terms from duplicated accumulation to one doubled 128-bit add | ~9.78k/s | ~4.05M/s | ~13.28k/s | ~81.85M/s | ~81.87M/s | ~311k/s | Filtered suite run with `--benchmark_min_time=1s` from `build/clang20-release/secp256k1_bench`; governor `powersave`, affinity `0-31`, benchmark reported CPU scaling warning |

## Interpretation

There are now four clear phases in the performance history so far.

### 1. Measurement got more honest

The first benchmark cut was useful for rough orientation, but fixed inputs understated the amount of variation in the hot path. Moving to deterministic mixed corpora gave a more realistic baseline and made later comparisons more trustworthy.

### 2. Fast field reduction was a major local win

The secp256k1-specific `ReduceModuloP` optimization moved reduction from roughly the sub-mega-op/s range into the high tens of millions of ops per second. That eliminated generic reduction as the dominant cost in raw field multiplication.

### 3. Projective/Jacobian formulas enabled the next major step

Once multiplication and reduction got much faster, inversions and affine-style formulas became the limiting factor. The later Jacobian point work removed many inversions from scalar multiplication and produced the next large jump, with that Jacobian snapshot showing point multiplication at about 3.58k ops/s and signature verification at about 2.10k ops/s.

### 4. Strauss-Shamir improved verification more than the core kernels

Once the point kernels were already in good shape, the next meaningful gain came from changing verification structure rather than raw field arithmetic. The Strauss-Shamir linear combination reuses doublings and mixed additions across `u1 * G + u2 * Q`, which pushed `VerifySignature` to about 3.18k ops/s while the standalone `PointMultiply` and `PointAdd` benchmarks moved only modestly. That pattern is exactly what this optimization should produce.

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
- `InvertModuloOdd / MultiplyModuloM`

Those ratios help show whether the next bottleneck is inversion, point formulas, or verification overhead outside the scalar-multiply core.

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
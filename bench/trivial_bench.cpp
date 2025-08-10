// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include <benchmark/benchmark.h>

static void BM_Trivial(benchmark::State& state) {
  for (auto _ : state) {
    int x = 1 + 2;
    benchmark::DoNotOptimize(x);
  }
}
BENCHMARK(BM_Trivial);

BENCHMARK_MAIN();

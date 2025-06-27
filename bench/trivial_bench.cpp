#include <benchmark/benchmark.h>

static void BM_Trivial(benchmark::State& state) {
  for (auto _ : state) {
    int x = 1 + 2;
    benchmark::DoNotOptimize(x);
  }
}
BENCHMARK(BM_Trivial);

BENCHMARK_MAIN();
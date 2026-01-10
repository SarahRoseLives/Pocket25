#!/usr/bin/env bash
set -euo pipefail

# Build and run the synthesis benchmark in scalar and SIMD modes for a quick comparison.

root_dir=$(cd "$(dirname "$0")/.." && pwd)

echo "== Build scalar (dev-release) =="
cmake --preset dev-release >/dev/null
cmake --build --preset dev-release -j >/dev/null

echo "== Build SIMD (dev-release-simd) =="
cmake --preset dev-release-simd -DMBELIB_BUILD_BENCHMARKS=ON >/dev/null
cmake --build --preset dev-release-simd -j bench_synth >/dev/null || cmake --build --preset dev-release-simd -j

scalar_bench="$root_dir/build/dev-release/bench_synth"
simd_bench="$root_dir/build/dev-release-simd/bench_synth"

if [[ ! -x "$scalar_bench" ]]; then
  echo "Building scalar benchmark (dev-release) with MBELIB_BUILD_BENCHMARKS=ON" >&2
  cmake --preset dev-release -DMBELIB_BUILD_BENCHMARKS=ON >/dev/null
  cmake --build --preset dev-release -j bench_synth >/dev/null || cmake --build --preset dev-release -j
fi

echo
echo "== Running scalar benchmark =="
"$scalar_bench"

echo
echo "== Running SIMD benchmark =="
"$simd_bench"


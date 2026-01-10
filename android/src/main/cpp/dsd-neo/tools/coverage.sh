#!/usr/bin/env bash
set -euo pipefail

# Simple coverage runner for protocol code paths.
# Defaults to P25 + DMR. Requires lcov/genhtml or llvm-cov depending on compiler.

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
BUILD_DIR="$ROOT_DIR/build/coverage-debug"

cmake --preset coverage-debug >/dev/null
cmake --build --preset coverage-debug -j >/dev/null
ctest --preset coverage-debug -V || true

if command -v lcov >/dev/null 2>&1; then
  echo "Generating lcov report..."
  pushd "$BUILD_DIR" >/dev/null
  lcov --capture --directory . --output-file coverage.info >/dev/null 2>&1 || true
  # Filter to protocol sources (default: P25 + DMR). Override with PROTO_COVERAGE="p25 dmr nxdn" etc.
  PROTO_COVERAGE=${PROTO_COVERAGE:-"p25 dmr"}
  EXTRACT_ARGS=()
  for proto in $PROTO_COVERAGE; do
    EXTRACT_ARGS+=("${ROOT_DIR}/src/protocol/${proto}/*")
  done
  # Always include shared DMR/P25 FEC helpers if present
  EXTRACT_ARGS+=("${ROOT_DIR}/src/fec/*")
  lcov --extract coverage.info "${EXTRACT_ARGS[@]}" -o coverage.filtered.info >/dev/null 2>&1 || true
  genhtml coverage.filtered.info --output-directory coverage_html >/dev/null 2>&1 || true
  popd >/dev/null
  echo "Coverage HTML: $BUILD_DIR/coverage_html/index.html"
else
  echo "lcov not found. For clang/llvm, run llvm-cov manually, e.g.:"
  echo "  PROTO_COVERAGE=\"p25 dmr\" cmake --preset coverage-debug && cmake --build --preset coverage-debug && ctest --preset coverage-debug -V"
  echo "  llvm-profdata merge -sparse *.profraw -o default.profdata"
  echo "  llvm-cov show ./tests/dsd-neo_test_dmr_crc_props -format=html -instr-profile=default.profdata > coverage.html"
fi

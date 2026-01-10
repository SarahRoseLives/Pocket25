#!/usr/bin/env bash
set -euo pipefail

# Run clang-tidy locally in a way that mirrors CI:
# - Ensures a compile_commands.json database exists (dev-debug preset)
# - Analyzes both sources and headers using the repo's .clang-tidy
# - Fails if any clang-analyzer-* or bugprone-* diagnostics are found

ROOT_DIR=$(git rev-parse --show-toplevel 2>/dev/null || pwd)
cd "$ROOT_DIR"

if ! command -v clang-tidy >/dev/null 2>&1; then
  echo "clang-tidy not found. Please install it (e.g., apt-get install clang-tidy)." >&2
  exit 1
fi
if ! command -v rg >/dev/null 2>&1; then
  echo "ripgrep (rg) not found. Please install it (e.g., apt-get install ripgrep)." >&2
  exit 1
fi

# Prefer compile_commands from the dev-debug preset; otherwise, use top-level if present.
PDB_DIR="build/dev-debug"
PDB_FILE="$PDB_DIR/compile_commands.json"
if [ ! -f "$PDB_FILE" ]; then
  if [ -f "compile_commands.json" ]; then
    PDB_DIR="."
  else
    echo "Configuring CMake preset 'dev-debug' to generate compile_commands.json..."
    cmake --preset dev-debug >/dev/null
  fi
fi

# Collect files: sources and headers within the repo (excluding build directory and external code)
mapfile -t FILES < <(git ls-files '*.c' '*.h' ':!:build/**' ':!:src/external/**')
if [ ${#FILES[@]} -eq 0 ]; then
  echo "No C sources/headers found to analyze."
  exit 0
fi

echo "Using compilation database: $PDB_DIR"
echo "Analyzing ${#FILES[@]} files with clang-tidy..."

# Run clang-tidy with project config and capture output
LOG_FILE=".clang-tidy.local.out"
clang-tidy -p "$PDB_DIR" "${FILES[@]}" 2>&1 | tee "$LOG_FILE" >/dev/null || true

# Fail if analyzer/bugprone diagnostics are present (treated as errors in CI)
if rg -n "\[(clang-analyzer|bugprone).*\]$" "$LOG_FILE" >/dev/null; then
  echo "clang-tidy found analyzer/bugprone issues. See $LOG_FILE for details." >&2
  exit 1
fi

echo "clang-tidy clean for analyzer/bugprone checks. Full output in $LOG_FILE"


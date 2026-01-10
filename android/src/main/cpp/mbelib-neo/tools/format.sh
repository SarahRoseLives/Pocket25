#!/usr/bin/env bash
set -euo pipefail

shopt -s globstar nullglob

# Include all C sources/headers across src, include, tests, and examples,
# plus any C/C headers at the repo root. Exclude build/ via git in CI.
files=(src/**/*.c src/**/*.h include/**/*.h tests/**/*.c tests/**/*.h examples/**/*.c examples/**/*.h *.c *.h)
if command -v clang-format >/dev/null 2>&1; then
  if [ ${#files[@]} -eq 0 ]; then
    echo "No C/C headers found to format."
    exit 0
  fi
  clang-format -i "${files[@]}"
  echo "Formatted ${#files[@]} files."
else
  echo "clang-format not found. Install it to run formatting." >&2
  exit 1
fi

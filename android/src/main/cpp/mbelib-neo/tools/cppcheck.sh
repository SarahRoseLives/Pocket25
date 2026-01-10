#!/usr/bin/env bash
set -euo pipefail

# Run cppcheck locally for static analysis:
# - Analyzes C/C++ sources with project-specific settings
# - Complements clang-tidy with different analysis techniques
# - Fails on error-level issues; warnings are informational

ROOT_DIR=$(git rev-parse --show-toplevel 2>/dev/null || pwd)
cd "$ROOT_DIR"

if ! command -v cppcheck >/dev/null 2>&1; then
  echo "cppcheck not found. Please install it (e.g., apt-get install cppcheck)." >&2
  exit 1
fi

# Parse arguments
STRICT=0
VERBOSE=0
while [[ $# -gt 0 ]]; do
  case $1 in
    --strict)
      STRICT=1
      shift
      ;;
    --verbose|-v)
      VERBOSE=1
      shift
      ;;
    --help|-h)
      echo "Usage: $0 [OPTIONS]"
      echo ""
      echo "Options:"
      echo "  --strict    Enable all checks and treat warnings as errors"
      echo "  --verbose   Show detailed output during analysis"
      echo "  -h, --help  Show this help message"
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 1
      ;;
  esac
done

echo "cppcheck version:"
cppcheck --version

# Detect number of CPU cores for parallel analysis
NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Build cppcheck arguments
# Note: cppcheck supports multiple --std flags; it applies the appropriate
# standard based on file extension (.c -> C standard, .cpp -> C++ standard)
CPPCHECK_ARGS=(
  --enable=warning,performance,portability
  --std=c11
  --std=c++14
  --suppress=missingIncludeSystem
  --inline-suppr
  -I include
  -I src/internal
  -I src/external/pffft
  -i src/external
  -j "$NPROC"
  --error-exitcode=1
)

# Strict mode: enable all checks and be more aggressive
if [[ $STRICT -eq 1 ]]; then
  echo "Strict mode: enabling all checks and treating warnings as errors"
  CPPCHECK_ARGS=(
    --enable=all
    --std=c11
    --std=c++14
    --suppress=missingIncludeSystem
    --inline-suppr
    -I include
    -I src/internal
    -I src/external/pffft
    -i src/external
    -j "$NPROC"
    --error-exitcode=1
  )
fi

# Verbose mode
if [[ $VERBOSE -eq 1 ]]; then
  CPPCHECK_ARGS+=(--verbose)
fi

# Suppress known false positives or low-value warnings for this codebase
# Format string mismatches with %d and unsigned are common in legacy code
CPPCHECK_ARGS+=(
  --suppress=invalidPrintfArgType_sint
  --suppress=invalidPrintfArgType_uint
  --suppress=normalCheckLevelMaxBranches
)

LOG_FILE=".cppcheck.local.out"

echo "Analyzing src/ and include/ directories..."
echo ""

# Run cppcheck and capture output
# Use --template for consistent output format
if cppcheck "${CPPCHECK_ARGS[@]}" \
  --template='{file}:{line}: {severity}: {message} [{id}]' \
  src/ include/ 2>&1 | tee "$LOG_FILE"; then
  echo ""
  echo "cppcheck passed. Full output in $LOG_FILE"
else
  EXIT_CODE=$?
  echo ""
  echo "cppcheck found issues. See $LOG_FILE for details." >&2

  # Print summary by severity
  echo ""
  echo "Summary by severity:" >&2
  grep -E ': (error|warning|style|performance|portability):' "$LOG_FILE" 2>/dev/null \
    | sed -E 's/.*: (error|warning|style|performance|portability):.*/\1/' \
    | sort | uniq -c | sort -rn >&2 || true

  exit $EXIT_CODE
fi

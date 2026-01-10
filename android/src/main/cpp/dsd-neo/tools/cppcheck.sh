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
usage() {
  cat <<'USAGE'
Usage: tools/cppcheck.sh [--strict] [--verbose|-v] [--] [files...]

Options:
  --strict    Enable all checks and treat warnings as errors.
  --verbose   Show detailed output during analysis.

Arguments:
  files...    Optional list of translation units to analyze (e.g., src/foo.c).
              When omitted, analyzes the src/ and include/ trees.
USAGE
}

STRICT=0
VERBOSE=0
REQUESTED_FILES=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --strict)
      STRICT=1
      shift
      ;;
    --verbose|-v)
      VERBOSE=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --)
      shift
      REQUESTED_FILES+=("$@")
      break
      ;;
    *)
      if [[ "$1" == -* ]]; then
        echo "Unknown option: $1" >&2
        usage >&2
        exit 1
      fi
      REQUESTED_FILES+=("$1")
      shift
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
  -i src/third_party
)

LOG_FILE=".cppcheck.local.out"

FILES=()
if [[ ${#REQUESTED_FILES[@]} -gt 0 ]]; then
  for f in "${REQUESTED_FILES[@]}"; do
    f="${f#./}"
    case "$f" in
      build/*|src/third_party/*) continue ;;
    esac
    case "$f" in
      *.c|*.cc|*.cpp|*.cxx) FILES+=("$f") ;;
    esac
  done

  if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "No translation units found to analyze from requested paths."
    exit 0
  fi

  mapfile -t FILES < <(printf '%s\n' "${FILES[@]}" | sort -u)
  echo "Analyzing ${#FILES[@]} file(s) with cppcheck..."
else
  echo "Analyzing src/ and include/ directories..."
fi
echo ""

# Select analysis targets.
CPPCHECK_TARGETS=(src/ include/)
if [[ ${#FILES[@]} -gt 0 ]]; then
  CPPCHECK_TARGETS=("${FILES[@]}")
fi

# Run cppcheck and capture output
# Use --template for consistent output format
if cppcheck "${CPPCHECK_ARGS[@]}" \
  --template='{file}:{line}: {severity}: {message} [{id}]' \
  "${CPPCHECK_TARGETS[@]}" 2>&1 | tee "$LOG_FILE"; then
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

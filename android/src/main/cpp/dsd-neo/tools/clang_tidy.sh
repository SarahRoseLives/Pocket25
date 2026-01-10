#!/usr/bin/env bash
set -euo pipefail

# Run clang-tidy locally in a way that mirrors CI:
# - Ensures a compile_commands.json database exists (dev-debug preset)
# - Analyzes translation units in the compilation database using the repo's .clang-tidy
# - Fails if any diagnostics are emitted as errors (WarningsAsErrors), or if clang-tidy can't process a file

ROOT_DIR=$(git rev-parse --show-toplevel 2>/dev/null || pwd)
cd "$ROOT_DIR"

usage() {
  cat <<'USAGE'
Usage: tools/clang_tidy.sh [--strict] [--all-commands] [--] [files...]

Options:
  --strict        Use .clang-tidy.strict (broader check set).
  --all-commands  Use the full compile_commands.json as-is. Note: if a source file
                  appears multiple times in the compilation database (e.g., built
                  for multiple targets), clang-tidy may process it multiple times
                  and its progress counter can exceed the unique file count.

Arguments:
  files...        Optional list of translation units to analyze (e.g., src/foo.c).
                  When omitted, analyzes all translation units in the compilation
                  database. Non-translation-unit paths (e.g., headers) are ignored.
USAGE
}

STRICT=0
ALL_COMMANDS=0
REQUESTED_FILES=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --strict) STRICT=1; shift ;;
    --all-commands) ALL_COMMANDS=1; shift ;;
    -h|--help) usage; exit 0 ;;
    --) shift; REQUESTED_FILES+=("$@"); break ;;
    -*)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *) REQUESTED_FILES+=("$1"); shift ;;
  esac
done

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

# Collect translation units from the compilation database (excluding build and third-party).
# clang-tidy may process a file multiple times if it has multiple compile commands.
PDB_FILE="$PDB_DIR/compile_commands.json"
TIDY_PDB_DIR="$PDB_DIR"
TIDY_PDB_TEMP_DIR=""
if command -v python3 >/dev/null 2>&1; then
  if [[ $ALL_COMMANDS -eq 0 ]]; then
    TIDY_PDB_TEMP_DIR=$(mktemp -d 2>/dev/null || mktemp -d -t dsd-neo-clang-tidy)
    trap 'rm -rf "$TIDY_PDB_TEMP_DIR" 2>/dev/null || true' EXIT
    TIDY_PDB_DIR="$TIDY_PDB_TEMP_DIR"
  fi

  mapfile -t FILES < <(python3 - "$PDB_FILE" "$ROOT_DIR" "$TIDY_PDB_DIR" "$ALL_COMMANDS" "${REQUESTED_FILES[@]}" <<'PY'
import json
import pathlib
import shlex
import sys

pdb_path = pathlib.Path(sys.argv[1])
root = pathlib.Path(sys.argv[2]).resolve()
out_dir = pathlib.Path(sys.argv[3]) if sys.argv[3] else None
all_commands = bool(int(sys.argv[4]))
requested = sys.argv[5:]

try:
    data = json.loads(pdb_path.read_text())
except Exception as exc:
    raise SystemExit(f"Failed to read {pdb_path}: {exc}")


def normalize_file(entry):
    file_field = entry.get("file")
    if not file_field:
        return None, None

    file_path = pathlib.Path(file_field)
    if not file_path.is_absolute():
        directory = entry.get("directory") or str(pdb_path.parent)
        file_path = pathlib.Path(directory) / file_path
    try:
        file_path = file_path.resolve()
    except Exception:
        file_path = file_path.absolute()

    try:
        rel = file_path.relative_to(root)
    except ValueError:
        return None, None

    if rel.parts and rel.parts[0] == "build":
        return None, None
    if len(rel.parts) >= 2 and rel.parts[0] == "src" and rel.parts[1] == "third_party":
        return None, None

    return str(rel), str(file_path)


entries_by_rel = {}
total_entries = 0
for entry in data:
    rel, abs_path = normalize_file(entry)
    if not rel:
        continue
    total_entries += 1
    entries_by_rel.setdefault(rel, []).append(entry)

unique_files = sorted(entries_by_rel.keys())
multi_cmd_files = sum(1 for cmds in entries_by_rel.values() if len(cmds) > 1)
extra_cmds = sum(len(cmds) - 1 for cmds in entries_by_rel.values() if len(cmds) > 1)

print(
    f"Compilation database entries: {total_entries} (unique files: {len(unique_files)}, "
    f"files with multiple commands: {multi_cmd_files}, extra commands: {extra_cmds})",
    file=sys.stderr,
)


def score_entry(rel_path, entry):
    cmd = entry.get("command") or ""
    args = entry.get("arguments")
    tokens = []
    if args:
        tokens = list(args)
    elif cmd:
        try:
            tokens = shlex.split(cmd)
        except Exception:
            tokens = cmd.split()

    score = 0
    # Prefer non-test compile commands when available.
    if any("/tests/" in t or t.startswith("tests/") for t in tokens):
        score -= 1000
    if any("tests/" in t for t in tokens if t.startswith("-I")):
        score -= 250
    # Prefer shared-library/object build flags.
    if "-fPIC" in tokens:
        score += 50
    if "-fPIE" in tokens:
        score -= 10
    # Prefer commands with more explicit defines/includes (tends to match real builds).
    score += sum(1 for t in tokens if t.startswith("-D"))
    score += sum(1 for t in tokens if t.startswith("-I"))
    # Tiebreaker: longer command line tends to be more complete.
    score += min(len(tokens), 500) // 10
    return score


def normalize_requested(path_str):
    p = pathlib.Path(path_str)
    if not p.is_absolute():
        p = (root / p).resolve()
    try:
        rel = p.relative_to(root)
    except ValueError:
        return None
    rel_str = rel.as_posix()
    if rel.parts and rel.parts[0] == "build":
        return None
    if len(rel.parts) >= 2 and rel.parts[0] == "src" and rel.parts[1] == "third_party":
        return None
    return rel_str


requested_rel = []
if requested:
    seen = set()
    for p in requested:
        rel = normalize_requested(p)
        if not rel or rel in seen:
            continue
        seen.add(rel)
        requested_rel.append(rel)


def is_translation_unit(path):
    suffix = pathlib.Path(path).suffix.lower()
    return suffix in {".c", ".cc", ".cpp", ".cxx"}


selected_files = unique_files
if requested_rel:
    requested_tus = [p for p in requested_rel if is_translation_unit(p)]
    missing = [p for p in requested_tus if p not in entries_by_rel]
    if missing:
        print(
            "Skipping files not present in compilation database:\n  "
            + "\n  ".join(missing),
            file=sys.stderr,
        )
    selected_files = sorted(p for p in requested_tus if p in entries_by_rel)


if out_dir and not all_commands:
    out_dir.mkdir(parents=True, exist_ok=True)
    selected = []
    for rel in selected_files:
        cmds = entries_by_rel.get(rel)
        if not cmds:
            continue
        best = max(cmds, key=lambda e: score_entry(rel, e))
        selected.append(best)

    out_path = out_dir / "compile_commands.json"
    out_path.write_text(json.dumps(selected, indent=2, sort_keys=True) + "\n")
    print(f"Wrote deduped compile database: {out_path} ({len(selected)} entries)", file=sys.stderr)

for path in selected_files:
    print(path)
PY
  )
else
  if [[ ${#REQUESTED_FILES[@]} -gt 0 ]]; then
    echo "python3 not found; analyzing requested files without compilation database filtering." >&2
    FILES=()
    for f in "${REQUESTED_FILES[@]}"; do
      f="${f#./}"
      case "$f" in
        build/*|src/third_party/*) continue ;;
      esac
      case "$f" in
        *.c|*.cc|*.cpp|*.cxx) FILES+=("$f") ;;
      esac
    done
  else
    echo "python3 not found; falling back to git ls-files (may include files not in compile database)." >&2
    mapfile -t FILES < <(git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' ':!:build/**' ':!:src/third_party/**')
  fi
fi
if [ ${#FILES[@]} -eq 0 ]; then
  if [[ ${#REQUESTED_FILES[@]} -gt 0 ]]; then
    echo "No translation units found to analyze from requested paths."
  else
    echo "No source files found to analyze."
  fi
  exit 0
fi

echo "Using compilation database: $TIDY_PDB_DIR"
echo "Analyzing ${#FILES[@]} files with clang-tidy..."
echo "clang-tidy version:"
clang-tidy --version | sed -n '1,2p'

# Run clang-tidy with project config and capture output
LOG_FILE=".clang-tidy.local.out"

# Optional strict mode: use alternate config enabling extra checks
CONFIG_FILE=".clang-tidy"
if [[ $STRICT -eq 1 ]]; then
  if [[ -f .clang-tidy.strict ]]; then
    CONFIG_FILE=".clang-tidy.strict"
    echo "Strict mode: using config $CONFIG_FILE"
  else
    echo "Strict mode requested, but .clang-tidy.strict not found; falling back to $CONFIG_FILE"
  fi
fi

if [[ -f "$CONFIG_FILE" ]]; then
  CFG_PATH=$(readlink -f "$CONFIG_FILE" 2>/dev/null || echo "$CONFIG_FILE")
  echo "Using config file: $CFG_PATH"
else
  echo "Config file not found: $CONFIG_FILE (clang-tidy will use built-in defaults)"
fi

clang-tidy -p "$TIDY_PDB_DIR" --config-file "$CONFIG_FILE" "${FILES[@]}" 2>&1 | tee "$LOG_FILE" >/dev/null || true

# Fail on error diagnostics (WarningsAsErrors) and on clang-tidy processing failures.
if rg -n "error:" "$LOG_FILE" >/dev/null; then
  echo "clang-tidy emitted diagnostics treated as errors. See $LOG_FILE for details." >&2
  echo "Summary (errors by check):" >&2
  rg -n "error:.*\\[[^]]+\\]$" "$LOG_FILE" | sed -E 's/.*\[([^]]+)\]$/\1/' | awk -F',' '{print $1}' | sort | uniq -c | sort -nr >&2
  exit 1
fi
if rg -n "^Error while processing " "$LOG_FILE" >/dev/null; then
  echo "clang-tidy failed to process one or more files. See $LOG_FILE for details." >&2
  rg -n "^Error while processing " "$LOG_FILE" >&2 || true
  exit 1
fi

echo "clang-tidy clean for error diagnostics. Full output in $LOG_FILE"

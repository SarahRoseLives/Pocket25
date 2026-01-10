#!/usr/bin/env bash
set -euo pipefail

# Install repo-provided Git hooks by pointing core.hooksPath at .githooks

repo_root=$(cd "$(dirname "$0")/.." && pwd)

git -C "$repo_root" config core.hooksPath .githooks
echo "Configured core.hooksPath to .githooks"

hooks_dir="$repo_root/.githooks"
if [[ -d "$hooks_dir" ]]; then
  shopt -s nullglob
  for hook in "$hooks_dir"/*; do
    if [[ -f "$hook" ]]; then
      chmod +x "$hook"
      echo "Enabled $(basename "$hook") hook."
    fi
  done
  shopt -u nullglob
fi

echo "Done. Commits will auto-format staged C/C++ files; pushes will run clang-tidy and cppcheck on changed translation units."

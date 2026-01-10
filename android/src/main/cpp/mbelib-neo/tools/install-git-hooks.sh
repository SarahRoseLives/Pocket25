#!/usr/bin/env bash
set -euo pipefail

# Install repo-provided Git hooks by pointing core.hooksPath at .githooks

repo_root=$(cd "$(dirname "$0")/.." && pwd)

git -C "$repo_root" config core.hooksPath .githooks
echo "Configured core.hooksPath to .githooks"

hook="$repo_root/.githooks/pre-commit"
if [[ -f "$hook" ]]; then
  chmod +x "$hook"
  echo "Enabled pre-commit hook (clang-format)."
fi

echo "Done. Commits will auto-format staged C/C headers."


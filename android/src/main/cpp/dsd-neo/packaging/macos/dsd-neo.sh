#!/usr/bin/env bash
set -euo pipefail

DIR=$(cd "$(dirname "$0")" && pwd)

# Prefer bundled libraries
export DYLD_FALLBACK_LIBRARY_PATH="$DIR/lib:${DYLD_FALLBACK_LIBRARY_PATH:-}"

exec "$DIR/bin/dsd-neo" "$@"


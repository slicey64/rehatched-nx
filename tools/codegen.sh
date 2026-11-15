#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REXGLUE="${1:-$ROOT/sdk/out/linux-amd64/rexglue}"
LOG="${2:-$ROOT/out/codegen.log}"
mkdir -p "$ROOT/out"
export REXGLUE_DUMP_IMAGE="$ROOT/out/renut_image.bin"
cd "$ROOT/app"
"$REXGLUE" codegen renut_manifest.toml > "$LOG" 2>&1 || {
    tail -n 30 "$LOG"
    exit 1
}
tail -n 10 "$LOG"

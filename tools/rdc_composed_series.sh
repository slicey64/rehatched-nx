#!/usr/bin/env bash
# rehatched-nx: RenderDoc captures of Ryujinx (window cycling, RDC_FRAMES consecutive frames each), then for each
# capture the game's own composed frame (the last R8G8B8A8 1920x1080 pass before the swapchain passes) into one grid.
#   bash tools/rdc_composed_series.sh <name> "<times>" <frames> [key=value ...]
set -u
NAME="$1"; TIMES="$2"; FRAMES="$3"; shift 3
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
D="${RYU_PORT_DIR:-$HOME/.config/Ryujinx/sdcard/switch/rehatched-nx}"
L="$ROOT/out/rdc/$NAME"
mkdir -p "$L"
cp "$D/rehatched.toml" "$L/rehatched.toml.orig"
{ for kv in "$@"; do printf '%s\n' "$kv"; done; cat "$L/rehatched.toml.orig"; } > "$D/rehatched.toml"
cd "$L" || exit 1
renderdoccmd capture --opt-hook-children --wait-for-exit --capture-file "$L/cap" ryujinx "$D/rehatched-nx.nro" \
  > "$L/ryujinx.txt" 2>&1 &
RP=$!
sleep 6
RDC_OUT="$L" RDC_TIMES="$TIMES" RDC_CYCLES=3 RDC_FRAMES="$FRAMES" timeout 200 qrenderdoc --python \
  "$ROOT/tools/rdc_trigger_cycle.py" > "$L/qrdc.txt" 2>&1
sleep 2
for p in $(pgrep -x Ryujinx.patched); do kill "$p"; done
sleep 3
kill "$RP" 2>/dev/null
cp "$L/rehatched.toml.orig" "$D/rehatched.toml"
mkdir -p "$L/composed"
for c in "$L"/capture_*.rdc; do
  [ "$(stat -c%s "$c")" -gt 5000000 ] || continue
  n=$(basename "$c" .rdc)
  P="$L/passes_$n"
  mkdir -p "$P"
  RDC_IN="$c" RDC_OUT="$P" timeout 300 qrenderdoc --python "$ROOT/tools/rdc_passes.py" > /dev/null 2>&1
  last=$(grep -E '1920x1080 R8G8B8A8_UNORM' "$P/passes.txt" | tail -1 | grep -oE 'pass_[0-9]+_e[0-9]+\.png')
  [ -n "$last" ] && cp "$P/$last" "$L/composed/$n.png"
  rm -rf "$P"
done
cd "$L/composed" && magick montage $(ls capture_*.png) -tile 6x -geometry 320x180+2+2 -label '%t' "$L/composed_grid.png"
echo "done: $(ls "$L/composed" | wc -l) composed frames -> $L/composed_grid.png"

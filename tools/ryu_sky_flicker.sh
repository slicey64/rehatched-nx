#!/usr/bin/env bash
# rehatched-nx: background flicker in Ryujinx. Attract mode, a burst of 10 consecutive frames every 10 s (14 bursts),
# and for each frame the sky band's mean brightness (black sky < 0.2, drawn sky > 0.5). Prints, from 60 s on, how many
# frames had a black sky and how many bursts mixed black and drawn frames (flicker).
#   bash tools/ryu_sky_flicker.sh <name> [key=value ...]
set -u
NAME="$1"; shift
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
D="${RYU_PORT_DIR:-$HOME/.config/Ryujinx/sdcard/switch/rehatched-nx}"
L="$ROOT/out/ryu_runs/skyf_$NAME"
mkdir -p "$L"
rm -f "$D"/capturas/*.png 2>/dev/null
cp "$D/rehatched.toml" "$L/rehatched.toml.orig"
{
  printf 'nfsmw_captura_cada_s = 10\nnfsmw_captura_max = 14\nbanjo_captura_rafaga = 10\nbanjo_captura_rafaga_ms = 1\n'
  for kv in "$@"; do printf '%s\n' "$kv"; done
  cat "$L/rehatched.toml.orig"
} > "$D/rehatched.toml"
timeout 150s ryujinx "$D/rehatched-nx.nro" > "$L/ryujinx.txt" 2>&1
cp "$L/rehatched.toml.orig" "$D/rehatched.toml"
mv "$D"/capturas/*.png "$L/" 2>/dev/null
cd "$L" || exit 1
for f in captura_*.png; do
  printf '%s %s\n' "$f" "$(magick "$f" -crop 50%x30%+25%+10% -format '%[fx:mean]' info:)"
done > sky.txt
python3 - "$NAME" "$*" <<'PYEOF'
import sys, collections
bursts = collections.defaultdict(list)
for line in open('sky.txt'):
    f, m = line.split()
    n = int(f.split('_')[1])
    if n >= 6:  # from 60 s: in the levels
        bursts[n].append(float(m))
frames = dark = mixed = 0
for n, ms in sorted(bursts.items()):
    frames += len(ms)
    d = sum(1 for m in ms if m < 0.2)
    dark += d
    if d and any(m > 0.5 for m in ms):
        mixed += 1
print(f"{sys.argv[1]}: {dark} black-sky frames of {frames}, {mixed} of {len(bursts)} bursts flicker  ({sys.argv[2]})")
PYEOF

#!/usr/bin/env bash
# rehatched-nx: count flashing frames in Ryujinx for one set of extra toml keys.
#   tools/ryu_flicker_ab.sh <name> [key=value ...]
# Runs the NRO in this port's Ryujinx folder for 150 s (attract mode), a burst of 10 frames every 5 s from 30 s on,
# and prints how many frames' mean brightness is more than 0.04 away from their burst's median. Restores the toml.
set -u
NAME="$1"; shift
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
D="${RYU_PORT_DIR:-$HOME/.config/Ryujinx/sdcard/switch/rehatched-nx}"
L="$ROOT/out/ryu_runs/ab_$NAME"
mkdir -p "$L"
rm -f "$D"/capturas/*.png 2>/dev/null
cp "$D/rehatched.toml" "$L/rehatched.toml.orig"
{
  printf 'nfsmw_captura_cada_s = 5\nnfsmw_captura_max = 30\nbanjo_captura_rafaga = 10\nbanjo_captura_rafaga_ms = 1\n'
  for kv in "$@"; do printf '%s\n' "$kv"; done
  cat "$L/rehatched.toml.orig"
} > "$D/rehatched.toml"
timeout 150s ryujinx "$D/rehatched-nx.nro" > "$L/ryujinx.txt" 2>&1
cp "$L/rehatched.toml.orig" "$D/rehatched.toml"
mv "$D"/capturas/*.png "$L/" 2>/dev/null
cp "$D/logs/$(ls -t "$D/logs" | head -1)" "$L/" 2>/dev/null
cd "$L" || exit 1
# Score: within each gameplay burst (from 30 s), the RMSE between consecutive frames at 1/4 size. A flash is a frame
# far from both neighbours (> 0.08 each way); "jumps" counts every step above 0.08.
python3 - "$NAME" <<'PYEOF'
import sys, glob, subprocess, collections
bursts = collections.defaultdict(list)
for f in sorted(glob.glob('captura_*.png')):
    n = int(f.split('_')[1])
    if n >= 6:
        bursts[n].append(f)
def rmse(a, b):
    r = subprocess.run(['magick', 'compare', '-metric', 'RMSE', '-resize', '25%', a, b, 'null:'],
                       capture_output=True, text=True)
    t = r.stderr.strip()
    return float(t[t.index('(') + 1:t.index(')')])
frames = flashes = jumps = 0
named = []
for n, fs in sorted(bursts.items()):
    d = [rmse(fs[i - 1], fs[i]) for i in range(1, len(fs))]
    frames += len(fs)
    jumps += sum(1 for x in d if x > 0.08)
    for i in range(1, len(fs) - 1):
        if d[i - 1] > 0.08 and d[i] > 0.08:
            flashes += 1
            named.append(fs[i])
print(f"{sys.argv[1]}: {flashes} flash frames, {jumps} jumps, in {frames} gameplay frames")
print("  " + " ".join(named[:6]))
PYEOF

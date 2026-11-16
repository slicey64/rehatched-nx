#!/usr/bin/env bash
# rehatched-nx: does level 1-1's background draw? One Ryujinx attract-mode run per variant (extra toml keys), a
# capture at 110 s (the attract demo is in level 1-1 then), and the mean brightness of the sky band (the top half's
# middle, where only the background draws). Black sky ~0.02, drawn sky ~0.6.
#   bash tools/ryu_sky_ab.sh <name> [key=value ...]
set -u
NAME="$1"; shift
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
D="${RYU_PORT_DIR:-$HOME/.config/Ryujinx/sdcard/switch/rehatched-nx}"
L="$ROOT/out/ryu_runs/sky_$NAME"
mkdir -p "$L"
rm -f "$D"/capturas/*.png 2>/dev/null
cp "$D/rehatched.toml" "$L/rehatched.toml.orig"
{
  printf 'nfsmw_captura_cada_s = 110\nnfsmw_captura_max = 1\n'
  for kv in "$@"; do printf '%s\n' "$kv"; done
  cat "$L/rehatched.toml.orig"
} > "$D/rehatched.toml"
timeout 125s ryujinx "$D/rehatched-nx.nro" > "$L/ryujinx.txt" 2>&1
cp "$L/rehatched.toml.orig" "$D/rehatched.toml"
mv "$D"/capturas/*.png "$L/" 2>/dev/null
f=$(ls "$L"/captura_*.png 2>/dev/null | head -1)
if [ -z "$f" ]; then echo "$NAME: no capture"; exit 0; fi
sky=$(magick "$f" -crop 50%x30%+25%+10% -format '%[fx:mean]' info:)
echo "$NAME: sky $sky  ($*)"

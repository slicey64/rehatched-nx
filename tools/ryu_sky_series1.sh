#!/usr/bin/env bash
# rehatched-nx: the deko3d-only keys, one at a time, for level 1-1's black background (tools/ryu_sky_ab.sh).
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
A=tools/ryu_sky_ab.sh
bash $A base
bash $A colorho_off "banjo_deko3d_color_handover = false"
bash $A drawres_off "banjo_deko3d_draw_resolves = false"
bash $A depthho_off "banjo_deko3d_depth_handover = false"
bash $A rr6_off "rr6_depth_covers_colour = false"
bash $A keep2 "rehatched_keep_unused_targets = 2"

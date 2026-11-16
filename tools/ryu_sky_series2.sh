#!/usr/bin/env bash
# rehatched-nx: background flicker, the Switch defaults' keys that differ from the PC's, with drawn resolves off.
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
A=tools/ryu_sky_flicker.sh
R="banjo_deko3d_draw_resolves = false"
bash $A dr_base "$R"
bash $A dr_eager_off "$R" "banjo_deko3d_eager_kicks = false"
bash $A dr_apiresolve_off "$R" "banjo_d3d_api_resolve = false"
bash $A dr_hostmips_on "$R" "banjo_renut_host_mips = true"
bash $A dr_kick_off "$R" "banjo_deko3d_kick_submit = false"

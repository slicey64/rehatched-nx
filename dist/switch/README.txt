rehatched-nx: Angry Birds Trilogy (Xbox 360, title 415608E4) on the Nintendo Switch
==================================================================================

A ReXGlue static recompilation of Angry Birds Trilogy, built with reNut's Direct3D-level
renderer on plume's native deko3d backend for the Nintendo Switch.

Your own game files are needed; nothing of the game is in this package except the recompiled code.

SD card layout
--------------
sdmc:/switch/rehatched-nx/rehatched-nx.nro      the program
sdmc:/switch/rehatched-nx/rehatched.toml        settings, the toml the NRO reads
sdmc:/switch/rehatched-nx/game_root/           the extracted game disc files: default.xex, stream.dat, audio.pck, database.xmplr...

The tomls
---------
The default settings are compiled into the NRO (app/rehatched_switch_defaults.toml), and it reads
rehatched.toml over them. Key settings:
- input_xbox_layout: false for Switch physical letter layout (A on right, B on bottom); true for Xbox positional layout.
- rehatched_keep_unused_targets: 1 keeps inactive depth targets across dynamic EDRAM view re-pointers, eliminating background flicker.
- banjo_renut_smaa: post-processing SMAA.

Credits
-------
- slicey64: ReHatched-NX port, XDK 2.0.9407.0 hook reverse engineering, EDRAM target retention, Linux toolchain.
- ChanseyIsTheBest: renut_nx_ultimate_base, renut-nx, and foundational Switch ports.
- masterspike52: reNut Direct3D-level renderer.
- Tom Clay: ReXGlue SDK.
- Ben Vanik & Xenia Project: foundational Xbox 360 research and emulation foundation.
- hedge-dev: XenosRecomp.
- fincs & devkitPro: deko3d, uam, libnx, devkitA64.
- StevensND: nfsmw-nx Horizon SDK layer.

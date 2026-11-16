# Angry Birds Trilogy (ReHatched-NX) Port Notes

## 1. Project Overview & Target Information
- **Title Name:** Angry Birds Trilogy
- **Title ID:** `415608E4`
- **Media ID:** `1A15DA04`
- **Base Version:** 0.0.0.2
- **Image Base:** `0x82000000`
- **Entry Point:** `0x8267B288`
- **Code Size:** `0x880000` (8,912,896 bytes)
- **Primary Goal:** Nintendo Switch native deko3d build (`renut.nro`)
- **Intermediate Goal:** Linux/PC build with Vulkan backend

## 2. Setup & Baseline
- Base template: `renut_nx_ultimate_base` cloned into `<BASE_DIR>` and instantiated at `<WORK_DIR>/rehatched-nx`.
- Web installer template: `renut_nx_ultimate_installer` cloned into `<INSTALLER_DIR>`.
- Game assets extracted directly from `the game disc image (title ID 415608E4)` into `<WORK_DIR>/game`.
- Symlinked `app/assets` -> `<WORK_DIR>/game`.
- Katamari artifacts stripped:
  - Removed Katamari PGO gcda files, `dist/switch/katamari-nx`, `tools/katamari_*.py`, `tools/renut_*katamari*`, and Katamari icons.
  - Reset `hand.txt`, `localized.txt`, `localized_bulk.txt`, `hot_functions.txt`, `hot_edges.txt`, `locals_optout.txt`, `lr_write_same_choice.json`, `r1_local_choice.json`, and `indirect_promotions.txt`.
  - Converted `katamari_keys.cpp` to `rehatched_keys.cpp` and `katamari_builtin_config.h.in` to `rehatched_builtin_config.h.in`.
  - Created `rehatched_switch.toml` and `rehatched_switch_defaults.toml`.
  - Set `REHATCHED_RENUT_HOOKS=OFF` initially for unhooked clean baseline.

## 3. Toolchain & Host Codegen
- Built host `rexglue` compiler for Linux x86_64 using Clang 23 with `-mssse3` support.
- Fixed `sdk/thirdparty/FFmpeg/config.h` for Linux/Switch support.
- Populated `app/config/renut_funcs.toml` with 139 tail-call and pointer-reached functions.
- Executed `tools/codegen.sh`: generated 187 C++ translation units in `app/generated/`.
- Executed `python3 tools/post_codegen.py`: all post-codegen passes applied successfully (loop barriers, direct calls, guest_cas, flush_mode_flow, vec_byteswap, ro_constants, float_loads, loop_locals, leaf_locals, store_cards, etc.).
- Calling Convention Verification:
  - `tools/cc_entry_census.py`: `none` (0 pointer-reached functions reading v0 or v32-v63 on entry).
  - `tools/cc_exit_census.py`: verified that all trailing reads after indirect calls are value-independent NOP moves (e.g. `mr r8, r8`).

## 4. Renut's Direct3D hooks on XDK 2.0.9407.0 (2026-11-15/16)

The black screen on the console: the game never got past the XDK's wait for the GPU. Found on the PC build with gdb
(attach is blocked by ptrace_scope, so the game runs under gdb and is interrupted), then fixed one stop at a time.

1. **Hooks never reached.** The hook addresses changed after `tools/post_codegen.py` had run, so `direct_calls.py`
   had turned the calls to them into direct `__imp__sub_X` calls (Direct3D_CreateDevice among them): the game made its
   device the XDK's way and spun in Swap's wait for a ring read pointer no GPU moves (`sub_8226C788`). Rule: **after
   any change to a hook address, run `python3 tools/post_codegen.py` in full** (`direct_calls.py --check` says 0).
2. **Wrong addresses.** The previous mapping had many hooks on the wrong functions. Each was checked against the guest
   code (`tools/hook_arity_check.py` compares argument registers read with the hook's parameters; the rest by the
   stores and calls). Corrected:

   | Function | Was | Is | Evidence |
   |---|---|---|---|
   | CreateTexture | unhooked | `sub_8226EF50` | 8 args, 0x34 header, type in r10 |
   | CreateSurface | `sub_8226EF50` | `sub_8226F070` | 5 args, 0x30 header, EDRAM allocation |
   | CreateVertexDeclaration | `sub_8227B6A0` (EDRAM allocator) | `sub_8227AD80` | counts 12-byte elements to 0xFF |
   | D3DVertexShader_Bind | unhooked | `sub_82280DE8` (5-arg wrapper) | first-release form with Flags and a 5th arg |
   | InsertCallback | `sub_82270FF0` (InsertFence) | not found, unhooked | InsertFence reads only the device |
   | BeginRingBig | `sub_82270458` (SetFence) | `sub_82271120` | BeginRing(count): room, KickOff |
   | BeginRingManual | `sub_82271120` | not identified, unhooked | |
   | SetGammaRamp / SetPWLGamma | 2-arg hooks | 3-arg wrappers | this XDK keeps Flags (ramp in r5) |
   | SetVertexShader | `sub_8227A858` | `sub_8227AA60` | stores +13072, reads the shader's +872 |
   | SetPixelShader | `sub_8227AC78` | `sub_8227A858` | stores +13068 |
   | SetVertexDeclaration | `sub_8227AA60` | `sub_8227AC78` | stores +12240 (SetFVF `sub_8227B3E8` too) |
   | Clear | `sub_8228D590` (internal) | `sub_8228DD48` | Count, pRects, Flags, D3DCOLOR, Z, Stencil |
   | ClearF | `sub_8228CFF0` (internal) | `sub_8228DC10` | NULL rect = the bound target's size |
   | Resolve | `sub_8228DC10` (ClearF) | `sub_8228CA80` | 11 args; calls internal `sub_8228B928` |
   | EndTiling (raw) | `sub_8228DD48` (Clear) | `sub_822815C0` | 8 args, called beside BeginTiling's caller |

   Device offsets (d3d.h, d3d_native_state.cpp) were rotated with them: vertex declaration +12240, pixel shader
   +13068, vertex shader +13072; the device's own (FVF) declaration is at +12568 (draw.cpp kDeviceDeclarationOffset).
3. Result on the PC (Vulkan, 2026-11-16): Activision logo, then the title screen ("Press Start"), 60 fps, ~70 draws a
   frame, no errors or verify differences (out/pc_runs/hooks9).

Open:
- The game also calls the internal resolve `sub_8228B928` directly (`sub_824891A0`, `sub_8249EE80`, `sub_824A05D0`)
  with two extra arguments (r6: a register value it writes in a PM4 packet; r7: a flag). Those resolves bypass the
  Resolve hook; to hook next if a gameplay picture lacks a pass.
- InsertCallback and BeginRingManual of this XDK are not identified.
- 24 of the base's resource hooks (Lock*, GetDesc, layouts) and some state hooks are still at Katamari addresses
  (unhooked); the guest's own versions run.

### Console run 1 (2026-11-16): fixes

The console ran the title screen and level 1 (no audio issue reported). Three faults, fixed:
- **Crash at the end of level 1** (logs/rex/rex_crash.log: a read of guest 0x70110001, the main stack's top, in
  `sub_8264B660`): the game's mesh code `sub_8261DF30` asks `D3DVertexDeclaration_GetDeclaration` (`sub_8227AE00`,
  unhooked) for a declaration's elements and searches them for a usage. The declarations are Renut's records, so the
  guest function copied nothing and the zeroed array had no D3DDECL_END: the search ran off the stack. Now hooked
  (shader.cpp), answering from the record as the original does (count + 1, D3DDECL_END from 0x8202E394).
- **Black, flickering level backgrounds**: the game's renderer calls the internal resolve `sub_8228B928` directly, so
  those resolves never reached the renderer. Now a raw hook maps its arguments onto Resolve's hook (draw.cpp). The PC's
  attract-mode levels (jungle, mine, city) draw their backgrounds (out/pc_runs/level1c); the console has to confirm.
- **A/B and X/Y swapped**: the shipped toml had `input_xbox_layout = true` (by position). Now `false` (by letter).

PC test automation: `input_script` on the command line keeps only its first press (the cvar parser splits at the
commas); put it in `app/out/pc_host/rehatched.toml` instead (and remove it after). The PC build leaves a 4.8 GB
`/dev/shm/xenia_memory_*` file behind when stopped by force: remove them after runs (21 of them starved Ryujinx).

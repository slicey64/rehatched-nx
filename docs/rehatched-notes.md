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

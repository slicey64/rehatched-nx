#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PY=python3
EXE="$ROOT/app/out/pc/xenos_recomp/build/XenosRecomp/XenosRecomp"
COMMON="$ROOT/app/thirdparty/reblue-XenosRecomp/XenosRecomp/shader_common.h"
DUMP="$ROOT/out/renut_shader_dump"
WORK="$ROOT/out/deko3d_shaders"
PKG="$ROOT/app/out/renut_switch"

mkdir -p "$WORK/early_alpha_hlsl" "$WORK/spirv" "$PKG"

export LD_LIBRARY_PATH=${HOME}/.local/lib:${LD_LIBRARY_PATH:-}

echo "== 1. Early-alpha HLSL dump"
export BANJO_HLSL_DUMP_ONLY=1
"$EXE" "$DUMP" "$WORK/early_alpha_pass1.cpp" "$COMMON" "$WORK/early_alpha_hlsl" || true
unset BANJO_HLSL_DUMP_ONLY

echo "== 2. Early-alpha processing"
"$PY" "$ROOT/tools/renut_early_alpha.py" "$WORK/early_alpha_hlsl"

echo "== 3. SPIR-V and deko3d shader cache generation"
export BANJO_HLSL_OVERRIDE="$WORK/early_alpha_hlsl"
export BANJO_DEKO3D_SPIRV_DIR="$WORK/spirv"
"$EXE" "$DUMP" "$WORK/shader_cache_deko3d.cpp" "$COMMON" || true
unset BANJO_HLSL_OVERRIDE
unset BANJO_DEKO3D_SPIRV_DIR

echo "== 4. Generate PC shader_cache.cpp for switch cache extraction"
mkdir -p "$ROOT/app/out/pc/renut_gpu"
export BANJO_HLSL_OVERRIDE="$WORK/early_alpha_hlsl"
"$EXE" "$DUMP" "$ROOT/app/out/pc/renut_gpu/shader_cache.cpp" "$COMMON" || true
unset BANJO_HLSL_OVERRIDE

echo "== 5. deko3d shaders (SPIRV-Cross -> GLSL -> uam check)"
cd "$ROOT"
"$PY" tools/deko3d_shaders.py --spirv "$WORK/spirv" --out "$WORK"

echo "== 6. deko3d GLSL optimization"
"$PY" tools/deko3d_glsl_opt.py --in "$WORK/glsl" --out "$WORK/glsl_opt"

echo "== 7. deko3d shader packaging"
"$PY" tools/deko3d_shader_package.py --glsl "$WORK/glsl_opt" --out "$PKG"

echo "== 8. renut_switch_shader_cache"
"$PY" tools/renut_switch_shader_cache.py --pc app/out/pc

echo "== Done switch shaders!"
ls -lh "$PKG"

#!/usr/bin/env bash
# banjo-nx (deko3d renderer, docs/deko3d-renderer.md): the PC tools the deko3d shader chain needs, in out/deko3d_tools:
# SPIRV-Cross (a PC tool only, Apache-2.0) and uam (app/thirdparty/uam, SwitchWakerHD's patched 1.1.0).
#   bash tools/deko3d_build_tools.sh [SPIRV-Cross source]
# rehatched-nx: Linux, macOS and Windows (devkitPro's MSYS2). With no SPIRV-Cross source given, a spirv-cross on PATH
# (the distribution's package, Homebrew's) is linked in instead of building one. The tools keep .exe names on Windows
# only, as the build (app/CMakeLists.txt BANJO_HOST_EXE) and tools/deko3d_*.py look for them.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/out/deko3d_tools"
EXE=""
STATIC=()
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        EXE=".exe"
        STATIC=(-DCMAKE_EXE_LINKER_FLAGS=-static)
        export PATH="/c/devkitPro/msys2/clang64/bin:/c/devkitPro/msys2/usr/bin:$PATH"
        ;;
esac
SPIRV_CROSS_SRC="${1:-}"

mkdir -p "$OUT/spirv-cross-build" "$OUT/uam-build"
if [ -n "$SPIRV_CROSS_SRC" ]; then
    cmake -S "$SPIRV_CROSS_SRC" -B "$OUT/spirv-cross-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DSPIRV_CROSS_ENABLE_TESTS=OFF -DSPIRV_CROSS_SHARED=OFF \
        -DSPIRV_CROSS_ENABLE_C_API=OFF ${STATIC[@]+"${STATIC[@]}"}
    ninja -C "$OUT/spirv-cross-build" spirv-cross
elif command -v spirv-cross >/dev/null; then
    ln -sf "$(command -v spirv-cross)" "$OUT/spirv-cross-build/spirv-cross$EXE"
    echo "spirv-cross: linked $(command -v spirv-cross)"
else
    echo "no spirv-cross: install your system's package, or pass a SPIRV-Cross source folder" >&2
    exit 1
fi

cmake -S "$ROOT/app/thirdparty/uam" -B "$OUT/uam-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ${STATIC[@]+"${STATIC[@]}"}
ninja -C "$OUT/uam-build" uam_tool
"$OUT/uam-build/uam$EXE" --version

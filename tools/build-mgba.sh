#!/usr/bin/env bash
# GBAR5 - builds the mGBA libretro core as a static archive for the PS5.
# SPDX-License-Identifier: MPL-2.0
#
#   tools/build-mgba.sh ps5      -> build/mgba-ps5/mgba_libretro.a
#   tools/build-mgba.sh desktop  -> build/mgba-desktop/mgba_libretro.a
#
# mGBA is CMake-built out of tree, so unlike the PSX cores it needs no patches.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
target=${1:-ps5}
core="$root/third_party/mgba"
out="$root/build/mgba-$target"
jobs=${BUILD_JOBS:-$(nproc 2>/dev/null || echo 4)}

[[ -f $core/CMakeLists.txt ]] || {
    echo "core source missing: third_party/mgba" >&2
    exit 2
}

common=(
    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_LIBRETRO=ON
    -DLIBRETRO_STATIC=ON
    -DBUILD_QT=OFF
    -DBUILD_SDL=OFF
    -DBUILD_GL=OFF
    -DBUILD_GLES2=OFF
    -DBUILD_SHARED=OFF
    -DUSE_FFMPEG=OFF
    -DUSE_PNG=OFF
    -DUSE_ZLIB=OFF
    -DUSE_ELF=OFF
    -DUSE_EPOXY=OFF
    -DUSE_DISCORD_RPC=OFF
    -DUSE_SQLITE3=OFF
    -DUSE_LIBZIP=OFF
    -DUSE_MINIZIP=OFF
    -DUSE_LZMA=OFF
)

case $target in
ps5)
    sdk="${PS5_PAYLOAD_SDK:-$root/.deps/native/ps5-payload-sdk}"
    [[ -d $sdk ]] || bash "$root/tools/setup-native-dependencies.sh" >/dev/null
    [[ -d $sdk ]] || { echo "PS5 payload SDK not found at $sdk" >&2; exit 2; }
    export PS5_PAYLOAD_SDK=$sdk USE_CCACHE=0
    # MGBA_PS5 also drives the small portability fixes in the core
    # (src/util/formatting.c: PS5 libc has no strtof_l).
    common+=(
        -DCMAKE_TOOLCHAIN_FILE="$root/tools/ps5-toolchain.cmake"
        -DCMAKE_C_FLAGS="-DMGBA_PS5 -O2 -fPIC -ffunction-sections -fdata-sections -march=znver2"
    )
    ;;
desktop)
    common+=(
        -DCMAKE_C_COMPILER="${CC:-gcc}"
        -DCMAKE_C_FLAGS="-O2 -fPIC -g"
    )
    ;;
*)
    echo "usage: tools/build-mgba.sh [ps5|desktop]" >&2
    exit 2
    ;;
esac

mkdir -p "$out"
cmake -S "$core" -B "$out" "${common[@]}"
ninja -C "$out" -j "$jobs" mgba_libretro
[[ -f $out/mgba_libretro.a ]] || { echo "mGBA archive was not produced" >&2; exit 2; }

# mGBA carries its own crc32(); zlib (through RADV) defines the same symbol at
# link time. Rename mGBA's copy in the archive; nothing else refers to it.
if [[ $target == ps5 ]]; then
    objcopy_bin=$(command -v llvm-objcopy-18 || command -v llvm-objcopy ||
        echo "$sdk/bin/llvm-objcopy")
    "$objcopy_bin" --redefine-sym crc32=mgba_crc32 "$out/mgba_libretro.a"
fi

ls -la "$out/mgba_libretro.a"
echo "==> [mgba] built"

#!/usr/bin/env bash
# PSXS5 - builds the network share clients for the PS5: libsmb2 (SMB 2/3) and
# libnfs (NFS), both LGPL-2.1, as one static archive for src/netfs.c.
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   tools/build-netfs.sh   -> build/netfs-ps5/libnetfs.a
#
# Each library is built by its own CMake files with tools/ps5-toolchain.cmake;
# the two are then linked into one object whose only global symbols are their
# APIs (smb2_*, nfs_*, rpc_*), so their MD5, SHA and AES code can't collide
# with anything else in the app.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/netfs-ps5"
sdk="${PS5_PAYLOAD_SDK:-$root/.deps/native/ps5-payload-sdk}"
[[ -d $sdk ]] || bash "$root/tools/setup-native-dependencies.sh" >/dev/null
export PS5_PAYLOAD_SDK=$sdk USE_CCACHE=0
for lib in libsmb2 libnfs; do
    [[ -f $root/third_party/$lib/CMakeLists.txt ]] || {
        echo "$lib source missing: run 'git submodule update --init third_party/$lib'" >&2
        exit 2
    }
done
llvm=$(dirname "$(command -v llvm-ar-18 || command -v llvm-ar)")
ar="$llvm/llvm-ar-18"; [[ -x $ar ]] || ar="$llvm/llvm-ar"
objcopy="$llvm/llvm-objcopy-18"; [[ -x $objcopy ]] || objcopy="$llvm/llvm-objcopy"
nm="$llvm/llvm-nm-18"; [[ -x $nm ]] || nm="$llvm/llvm-nm"
lld=$(command -v ld.lld-18 || command -v ld.lld)

stamp="$out/.stamp"
want="$(git -C "$root/third_party/libsmb2" rev-parse HEAD) $(git -C "$root/third_party/libnfs" rev-parse HEAD) $(cat "$0" "$root/tools/ps5-toolchain.cmake" | sha256sum | cut -c1-16)"
if [[ -f $out/libnetfs.a && -f $stamp && $(cat "$stamp") == "$want" ]]; then
    echo "==> [netfs] $out/libnetfs.a"
    exit 0
fi

chmod +x "$root/tooling/prospero-clang18"
rm -rf "$out"
mkdir -p "$out"
common=(-G Ninja -DCMAKE_TOOLCHAIN_FILE="$root/tools/ps5-toolchain.cmake" -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF)
echo "==> [netfs] libsmb2"
cmake -S "$root/third_party/libsmb2" -B "$out/smb2" "${common[@]}" -DENABLE_EXAMPLES=OFF -DENABLE_UTILS=OFF \
    -DENABLE_GSSAPI=OFF -DENABLE_LIBKRB5=OFF -DENABLE_LIBDCERPC=OFF >"$out/smb2.log" 2>&1 || { cat "$out/smb2.log"; exit 1; }
cmake --build "$out/smb2" >>"$out/smb2.log" 2>&1 || { tail -60 "$out/smb2.log"; exit 1; }
echo "==> [netfs] libnfs"
cmake -S "$root/third_party/libnfs" -B "$out/nfs" "${common[@]}" -DENABLE_UTILS=OFF -DENABLE_EXAMPLES=OFF \
    -DENABLE_TESTS=OFF -DENABLE_DOCUMENTATION=OFF -DENABLE_MULTITHREADING=OFF >"$out/nfs.log" 2>&1 ||
    { cat "$out/nfs.log"; exit 1; }
cmake --build "$out/nfs" >>"$out/nfs.log" 2>&1 || { tail -60 "$out/nfs.log"; exit 1; }

echo "==> [netfs] one archive"
mapfile -t archives < <(find "$out/smb2" "$out/nfs" -name '*.a' | sort)
(( ${#archives[@]} > 0 )) || { echo "no archives built" >&2; exit 1; }
"$lld" -r --whole-archive "${archives[@]}" -o "$out/netfs.o"
"$nm" --defined-only --extern-only "$out/netfs.o" | awk '{print $NF}' | grep -E '^(smb2_|nfs_|rpc_|nfs4_)' |
    sort -u >"$out/api.txt"
"$objcopy" --keep-global-symbols="$out/api.txt" "$out/netfs.o" "$out/netfs-local.o"
"$ar" rcs "$out/libnetfs.a" "$out/netfs-local.o"
mkdir -p "$out/include"
cp -r "$root/third_party/libsmb2/include/smb2" "$out/include/"
cp -r "$root/third_party/libnfs/include/nfsc" "$out/include/"
echo "$want" >"$stamp"
echo "==> [netfs] $(wc -l <"$out/api.txt") functions, $out/libnetfs.a"

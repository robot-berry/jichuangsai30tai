#!/bin/sh
set -eu

cd "$(dirname "$0")"

mkdir -p link_compat
if [ ! -e link_compat/libdw.so ] && [ -e /usr/lib/aarch64-linux-gnu/libdw.so.1 ]; then
  ln -sf /usr/lib/aarch64-linux-gnu/libdw.so.1 link_compat/libdw.so
fi

SWAP_FILE="${SWAP_FILE:-$PWD/codex_build.swap}"
if ! swapon --show=NAME | grep -qx "$SWAP_FILE"; then
  if [ ! -f "$SWAP_FILE" ]; then
    fallocate -l 2G "$SWAP_FILE" 2>/dev/null || dd if=/dev/zero of="$SWAP_FILE" bs=1M count=2048
    chmod 600 "$SWAP_FILE"
    mkswap "$SWAP_FILE" >/dev/null
  fi
  swapon "$SWAP_FILE" 2>/dev/null || true
fi

cmake -S . -B build/ZG \
  -DTARGET_CHIP=ZG \
  -DCMAKE_PREFIX_PATH=/usr/cmake \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_CXX_FLAGS="-O0 -g0" \
  -DCMAKE_C_FLAGS="-O0 -g0"

cmake --build build/ZG -j1

if cmake --build build/ZG --target stage_bundle; then
  echo "stage_bundle finished"
else
  echo "stage_bundle target not available, using build/ZG output"
fi

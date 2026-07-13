#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build/cross-ZG}"
ICRAFT_SDK_ROOT="${ICRAFT_SDK_ROOT:-/home/ly/icraft_sdk/3.33.1-board}"
DEPS_DIR="${DEPS_DIR:-$ROOT_DIR/../../../fpai_demo_package_26010502/deps}"
JOBS="${JOBS:-4}"

CMAKE_DIR="$ICRAFT_SDK_ROOT/usr/cmake"
VERSION_FILE="$CMAKE_DIR/icraft-zg330backend-config-version.cmake"

if [ ! -x /usr/bin/aarch64-linux-gnu-g++ ]; then
    echo "Missing /usr/bin/aarch64-linux-gnu-g++" >&2
    exit 1
fi
if [ ! -f "$VERSION_FILE" ] || ! grep -q '3.33.1.0' "$VERSION_FILE"; then
    echo "Icraft 3.33.1 ZG330 SDK not found at: $ICRAFT_SDK_ROOT" >&2
    exit 1
fi
if [ ! -d "$DEPS_DIR/modelzoo_utils" ] || [ ! -d "$DEPS_DIR/thirdparty" ]; then
    echo "FPAI dependencies not found at: $DEPS_DIR" >&2
    exit 1
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DTARGET_CHIP=ZG \
    -DDEPS_DIR="$DEPS_DIR" \
    -DCMAKE_PREFIX_PATH="$CMAKE_DIR" \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=/usr/bin/aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=/usr/bin/aarch64-linux-gnu-g++ \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DCMAKE_C_FLAGS=-O0 \
    -DCMAKE_CXX_FLAGS=-O0

cmake --build "$BUILD_DIR" -- -j"$JOBS"

OUTPUT="$BUILD_DIR/sdicamera+yolov5+hdmi"
file "$OUTPUT"
sha256sum "$OUTPUT"

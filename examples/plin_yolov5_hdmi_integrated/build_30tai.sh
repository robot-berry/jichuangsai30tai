#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build/ZG"
DEPLOY_DIR="${BUILD_DIR}/deploy/ZG"

echo "[1/4] Configure 30TAI (ZG) build"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DTARGET_CHIP=ZG -DCMAKE_PREFIX_PATH=/usr/cmake

echo "[2/4] Build executable"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "[3/4] Stage runtime bundle"
cmake --build "${BUILD_DIR}" --target stage_bundle

echo "[4/4] Done"
echo "Runtime bundle: ${DEPLOY_DIR}"
echo "Board run example:"
echo "  cd /home/fmsh/fpai_demo"
echo "  chmod a+x sdicamera+yolov5+hdmi"
echo "  ./sdicamera+yolov5+hdmi configs/ZG/sdicamera+yolov5+hdmi.yaml"

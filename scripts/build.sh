#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
STIQY_BUILD="${ROOT}/../StiQy_2.0/build-release"

if [[ ! -f "${STIQY_BUILD}/common/image/libcommon_image.a" ]]; then
  echo "StiQy prebuilt libs missing. Build StiQy first:"
  echo "  cd ../StiQy_2.0 && mkdir -p build-release && cd build-release && cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build . -j"
  exit 1
fi

CUDA_ROOT="/usr/local/cuda-13.0"
if [[ ! -d "${CUDA_ROOT}" ]]; then
  CUDA_ROOT="/usr/local/cuda-13"
fi
if [[ ! -d "${CUDA_ROOT}" ]]; then
  CUDA_ROOT="/usr/local/cuda"
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake "${ROOT}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSTIQY_ROOT="${ROOT}/../StiQy_2.0" \
  -DSTIQY_BUILD_DIR="${STIQY_BUILD}" \
  -DCUDAToolkit_ROOT="${CUDA_ROOT}" \
  -DCUDA_TOOLKIT_ROOT_DIR="${CUDA_ROOT}" \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-12 \
  -DCMAKE_C_COMPILER=/usr/bin/gcc-12 \
  -DPython_EXECUTABLE=/usr/bin/python3.12

cmake --build . -j"$(nproc)"
echo "Built: ${ROOT}/bin/sponsor_tracker"

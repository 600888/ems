#!/usr/bin/env bash
set -euo pipefail

MODE="${1:-native}"   # native | cross
BUILD_TYPE="${2:-Release}"
JOBS="${JOBS:-$(nproc)}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BACKEND_DIR="${ROOT_DIR}/backend"

if [[ ! -f "${BACKEND_DIR}/CMakeLists.txt" ]]; then
  echo "backend/CMakeLists.txt not found"
  exit 1
fi

cmake_args=(
  -S "${BACKEND_DIR}"
  -D "CMAKE_BUILD_TYPE=${BUILD_TYPE}"
  -D "EMS_ENABLE_ARCH_TUNING=ON"
)

case "${MODE}" in
  native)
    build_dir="${BACKEND_DIR}/build/linux-arm-native"
    cmake_args+=(
      -B "${build_dir}"
      -D "EMS_TARGET_LINUX_ARM=ON"
      -D "BUILD_TESTS=OFF"
    )
    ;;
  cross)
    build_dir="${BACKEND_DIR}/build/linux-arm-cross"
    cmake_args+=(
      -B "${build_dir}"
      -D "BUILD_TESTS=OFF"
      -D "CMAKE_TOOLCHAIN_FILE=${BACKEND_DIR}/cmake/toolchains/linux-arm-aarch64-linux-gnu.cmake"
    )
    ;;
  *)
    echo "Usage: $0 [native|cross] [Release|Debug|RelWithDebInfo|MinSizeRel]"
    exit 1
    ;;
esac

echo "[1/2] Configure: mode=${MODE}, build_type=${BUILD_TYPE}"
cmake "${cmake_args[@]}"

echo "[2/2] Build: ${build_dir}"
cmake --build "${build_dir}" -j "${JOBS}"

echo "Done. Output directory: ${build_dir}"

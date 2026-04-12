#!/usr/bin/env bash
set -euo pipefail

MODE="${1:-native}"   # native | cross
BUILD_TYPE="${2:-Release}"
JOBS="${JOBS:-$(nproc)}"
PYTHON_EXECUTABLE="${PYTHON_EXECUTABLE:-/usr/bin/python3}"
PYTHON_INCLUDE_DIR="${PYTHON_INCLUDE_DIR:-}"
PYTHON_LIBRARY="${PYTHON_LIBRARY:-}"

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

    if [[ -z "${PYTHON_INCLUDE_DIR}" ]]; then
      for candidate in \
        /usr/include/aarch64-linux-gnu/python3.12 \
        /usr/include/aarch64-linux-gnu/python3.11 \
        /usr/include/aarch64-linux-gnu/python3.10 \
        /usr/include/python3.12 \
        /usr/include/python3.11 \
        /usr/include/python3.10
      do
        if [[ -f "${candidate}/Python.h" ]]; then
          PYTHON_INCLUDE_DIR="${candidate}"
          break
        fi
      done
    fi

    if [[ -z "${PYTHON_LIBRARY}" ]]; then
      for candidate in \
        /usr/lib/aarch64-linux-gnu/libpython3.12.so \
        /usr/lib/aarch64-linux-gnu/libpython3.11.so \
        /usr/lib/aarch64-linux-gnu/libpython3.10.so \
        /usr/lib/aarch64-linux-gnu/libpython3.so
      do
        if [[ -f "${candidate}" ]]; then
          PYTHON_LIBRARY="${candidate}"
          break
        fi
      done
    fi

    cmake_args+=(
      -B "${build_dir}"
      -D "BUILD_TESTS=OFF"
      -D "CMAKE_TOOLCHAIN_FILE=${BACKEND_DIR}/cmake/toolchains/linux-arm-aarch64-linux-gnu.cmake"
      -D "Python_EXECUTABLE=${PYTHON_EXECUTABLE}"
    )

    if [[ -n "${PYTHON_INCLUDE_DIR}" ]]; then
      cmake_args+=(
        -D "Python_INCLUDE_DIR=${PYTHON_INCLUDE_DIR}"
        -D "Python_INCLUDE_DIRS=${PYTHON_INCLUDE_DIR}"
      )
    fi

    if [[ -n "${PYTHON_LIBRARY}" ]]; then
      cmake_args+=(
        -D "Python_LIBRARY=${PYTHON_LIBRARY}"
        -D "Python_LIBRARIES=${PYTHON_LIBRARY}"
      )
    fi

    if [[ -z "${PYTHON_INCLUDE_DIR}" || -z "${PYTHON_LIBRARY}" ]]; then
      echo "Linux ARM cross build requires ARM64 Python development files."
      echo "Missing detected paths:"
      echo "  PYTHON_INCLUDE_DIR=${PYTHON_INCLUDE_DIR:-<not found>}"
      echo "  PYTHON_LIBRARY=${PYTHON_LIBRARY:-<not found>}"
      echo
      echo "On Ubuntu/Debian, install them with:"
      echo "  sudo dpkg --add-architecture arm64"
      echo "  sudo apt-get update"
      echo "  sudo apt-get install -y python3-dev:arm64 libpython3-dev:arm64"
      echo
      echo "Or override manually:"
      echo "  PYTHON_INCLUDE_DIR=/usr/include/aarch64-linux-gnu/python3.12"
      echo "  PYTHON_LIBRARY=/usr/lib/aarch64-linux-gnu/libpython3.12.so"
      exit 1
    fi
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

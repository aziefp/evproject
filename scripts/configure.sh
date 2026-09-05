#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-${project_dir}/build-debug}"
build_type="${BUILD_TYPE:-Debug}"

qt_dir="${QT_DIR:-}"
user_home="${HOME:-}"
if [[ -z "${qt_dir}" && -n "${user_home}" && -d "${user_home}/Qt/6.11.2/gcc_64" ]]; then
  qt_dir="${user_home}/Qt/6.11.2/gcc_64"
fi
if [[ -z "${qt_dir}" ]] && command -v qmake6 >/dev/null 2>&1; then
  qt_dir="$(qmake6 -query QT_INSTALL_PREFIX)"
fi
if [[ -z "${qt_dir}" ]]; then
  echo "未找到 Qt 6.11。请安装 Qt，或用 QT_DIR=/path/to/Qt/6.11.x/gcc_64 指定位置。" >&2
  exit 1
fi

qt_base="$(dirname "$(dirname "${qt_dir}")")"
cmake_bin="${CMAKE_BIN:-}"
if [[ -z "${cmake_bin}" && -x "${qt_base}/Tools/CMake/bin/cmake" ]]; then
  cmake_bin="${qt_base}/Tools/CMake/bin/cmake"
fi
if [[ -z "${cmake_bin}" ]]; then
  cmake_bin="$(command -v cmake || true)"
fi
ninja_bin="${NINJA_BIN:-}"
if [[ -z "${ninja_bin}" && -x "${qt_base}/Tools/Ninja/ninja" ]]; then
  ninja_bin="${qt_base}/Tools/Ninja/ninja"
fi
if [[ -z "${ninja_bin}" ]]; then
  ninja_bin="$(command -v ninja || true)"
fi
if [[ -z "${cmake_bin}" || -z "${ninja_bin}" ]]; then
  echo "未找到 CMake 或 Ninja，请安装后重试。" >&2
  exit 1
fi

"${cmake_bin}" -S "${project_dir}" -B "${build_dir}" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="${ninja_bin}" \
  -DCMAKE_PREFIX_PATH="${qt_dir}" \
  -DCMAKE_BUILD_TYPE="${build_type}"

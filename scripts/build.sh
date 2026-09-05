#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-${project_dir}/build-debug}"

cmake_bin="${CMAKE_BIN:-}"
user_home="${HOME:-}"
if [[ -z "${cmake_bin}" && -n "${user_home}" && -x "${user_home}/Qt/Tools/CMake/bin/cmake" ]]; then
  cmake_bin="${user_home}/Qt/Tools/CMake/bin/cmake"
fi
if [[ -z "${cmake_bin}" ]]; then
  cmake_bin="$(command -v cmake || true)"
fi
if [[ -z "${cmake_bin}" ]]; then
  echo "未找到 CMake，请安装后重试。" >&2
  exit 1
fi

if [[ ! -f "${build_dir}/build.ninja" ]]; then
  "${project_dir}/scripts/configure.sh"
fi
"${cmake_bin}" --build "${build_dir}" --parallel

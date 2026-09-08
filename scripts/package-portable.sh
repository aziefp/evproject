#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-${project_dir}/build-portable}"
output_root="${PACKAGE_OUTPUT_ROOT:-${project_dir}/便捷运行包}"
package_name="${PACKAGE_NAME:-BIT比特充电-Linux-x86_64}"
package_dir="${output_root}/${package_name}"
key_path="${project_dir}/key.txt"

if [[ ! -s "${key_path}" ]]; then
    echo "未找到非空 key.txt，无法制作可用的地图演示包。" >&2
    exit 1
fi

qt_dir="${QT_DIR:-}"
user_home="${HOME:-}"
if [[ -z "${qt_dir}" && -n "${user_home}" && -d "${user_home}/Qt/6.11.2/gcc_64" ]]; then
    qt_dir="${user_home}/Qt/6.11.2/gcc_64"
fi
if [[ -z "${qt_dir}" ]] && command -v qmake6 >/dev/null 2>&1; then
    qt_dir="$(qmake6 -query QT_INSTALL_PREFIX)"
fi
if [[ -z "${qt_dir}" ]]; then
    echo "未找到 Qt 6.11，可用 QT_DIR=/path/to/Qt/6.11.x/gcc_64 指定。" >&2
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
if [[ -z "${cmake_bin}" ]]; then
    echo "未找到 CMake。" >&2
    exit 1
fi

BUILD_DIR="${build_dir}" BUILD_TYPE=Release QT_DIR="${qt_dir}" \
    "${project_dir}/scripts/configure.sh"
BUILD_DIR="${build_dir}" CMAKE_BIN="${cmake_bin}" \
    "${project_dir}/scripts/build.sh"

mkdir -p "${output_root}"
case "${package_dir}" in
    "${output_root}"/*) ;;
    *)
        echo "拒绝清理输出目录：路径超出便捷运行包根目录。" >&2
        exit 1
        ;;
esac
"${cmake_bin}" -E remove_directory "${package_dir}"
"${cmake_bin}" --install "${build_dir}" --prefix "${package_dir}"

"${cmake_bin}" -E copy "${key_path}" "${package_dir}/key.txt"
chmod 600 "${package_dir}/key.txt"
mkdir -p "${package_dir}/runtime"
chmod 755 "${package_dir}/运行演示.sh" "${package_dir}/检查完整性.sh"

"${package_dir}/检查完整性.sh"
echo "便携包已生成：${package_dir}"

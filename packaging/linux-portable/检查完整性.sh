#!/usr/bin/env bash
set -euo pipefail

package_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="${package_dir}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

required_files=(
    "bin/ev-admin-server"
    "bin/ev-user-client"
    "bin/qt.conf"
    "libexec/QtWebEngineProcess"
    "libexec/qt.conf"
    "resources/icudtl.dat"
    "resources/qtwebengine_resources.pak"
    "translations/qtwebengine_locales/en-US.pak"
    "plugins/platforms/libqxcb.so"
    "plugins/platforms/libqoffscreen.so"
    "plugins/sqldrivers/libqsqlite.so"
    "key.txt"
)

failed=0
for relative_path in "${required_files[@]}"; do
    if [[ ! -s "${package_dir}/${relative_path}" ]]; then
        echo "缺少：${relative_path}" >&2
        failed=1
    fi
done

if ! grep -Eq '^Prefix[[:space:]]*=[[:space:]]*\.\.$' "${package_dir}/bin/qt.conf" \
    || ! grep -Eq '^Prefix[[:space:]]*=[[:space:]]*\.\.$' "${package_dir}/libexec/qt.conf"; then
    echo "qt.conf 不是可移动的相对路径配置。" >&2
    failed=1
fi

if ! command -v ldd >/dev/null 2>&1; then
    echo "未找到系统 ldd，已完成文件检查，无法执行动态库检查。" >&2
    exit 2
fi

check_elf() {
    local relative_path="$1"
    local missing
    missing="$(ldd "${package_dir}/${relative_path}" 2>&1 | awk '/not found/ {print}')"
    if [[ -n "${missing}" ]]; then
        echo "${relative_path} 存在未解析依赖：" >&2
        echo "${missing}" >&2
        failed=1
    fi
}

check_elf "bin/ev-admin-server"
check_elf "bin/ev-user-client"
check_elf "libexec/QtWebEngineProcess"
check_elf "plugins/platforms/libqxcb.so"
check_elf "plugins/platforms/libqoffscreen.so"
check_elf "plugins/sqldrivers/libqsqlite.so"

if [[ "${failed}" -ne 0 ]]; then
    echo "检查失败：便携包文件或当前 Linux 系统依赖不完整。" >&2
    exit 1
fi

echo "检查通过：程序、Qt 运行库、插件、WebEngine 资源和 key.txt 齐全，当前系统无未解析动态库。"

#!/usr/bin/env bash
set -euo pipefail

# 便携包单机入口：只启动已打包程序，不编译、不安装、不创建后台服务。
package_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
server_bin="${package_dir}/bin/ev-admin-server"
client_bin="${package_dir}/bin/ev-user-client"
database_path="${package_dir}/runtime/evplatform.db"
key_path="${package_dir}/key.txt"

export LD_LIBRARY_PATH="${package_dir}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export QT_PLUGIN_PATH="${package_dir}/plugins"
export QTWEBENGINEPROCESS_PATH="${package_dir}/libexec/QtWebEngineProcess"
export QTWEBENGINE_RESOURCES_PATH="${package_dir}/resources"
export QTWEBENGINE_LOCALES_PATH="${package_dir}/translations/qtwebengine_locales"

if [[ ! -x "${server_bin}" || ! -x "${client_bin}" ]]; then
    echo "便携包不完整：找不到可执行程序。" >&2
    exit 1
fi
if [[ ! -s "${key_path}" ]]; then
    echo "便携包不完整：key.txt 不存在或为空。" >&2
    exit 1
fi

mkdir -p "${package_dir}/runtime"
cd "${package_dir}"
"${server_bin}" \
    --listen-address 0.0.0.0 \
    --port 45454 \
    --db "${database_path}" \
    --key-file "${key_path}" \
    --simulation-speed 60 \
    >"${package_dir}/runtime/admin-server.log" 2>&1 &
server_pid=$!

cleanup() {
    kill "${server_pid}" 2>/dev/null || true
    wait "${server_pid}" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

sleep 1
if ! kill -0 "${server_pid}" 2>/dev/null; then
    echo "管理服务器启动失败，请查看 runtime/admin-server.log。" >&2
    exit 1
fi

"${client_bin}" --host 127.0.0.1 --port 45454 --key-file "${key_path}"

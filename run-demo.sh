#!/usr/bin/env bash
set -euo pipefail

# 透明的单机演示入口：增量编译 -> 启动服务器 -> 启动用户端。
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${BUILD_DIR:-${project_dir}/build-debug}"
server_bin="${build_dir}/apps/admin-server/ev-admin-server"
client_bin="${build_dir}/apps/user-client/ev-user-client"
listen_address="${LISTEN_ADDRESS:-0.0.0.0}"
server_host="${SERVER_HOST:-127.0.0.1}"
server_port="${SERVER_PORT:-45454}"
database_path="${DB_PATH:-runtime/evplatform.db}"
simulation_speed="${SIMULATION_SPEED:-60}"

"${project_dir}/scripts/build.sh"

cd "${project_dir}"
"${server_bin}" \
  --listen-address "${listen_address}" \
  --port "${server_port}" \
  --db "${database_path}" \
  --key-file key.txt \
  --simulation-speed "${simulation_speed}" &
server_pid=$!

cleanup() {
  kill "${server_pid}" 2>/dev/null || true
  wait "${server_pid}" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

sleep 1
if ! kill -0 "${server_pid}" 2>/dev/null; then
  echo "服务器启动失败，请查看上方错误信息。" >&2
  exit 1
fi

"${client_bin}" --host "${server_host}" --port "${server_port}" --key-file key.txt

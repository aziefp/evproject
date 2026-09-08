#!/usr/bin/env bash
set -euo pipefail

# 透明的演示入口：默认单机启动两端，也可指定 server/client 用于两机。
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${BUILD_DIR:-${project_dir}/build-debug}"
server_bin="${build_dir}/apps/admin-server/ev-admin-server"
client_bin="${build_dir}/apps/user-client/ev-user-client"
mode="${1:-all}"
listen_address="${LISTEN_ADDRESS:-0.0.0.0}"
server_host="${2:-${SERVER_HOST:-127.0.0.1}}"
server_port="${SERVER_PORT:-45454}"
database_path="${DB_PATH:-runtime/evplatform.db}"
simulation_speed="${SIMULATION_SPEED:-60}"

usage() {
  cat <<'EOF'
用法：
  ./run-demo.sh                         # 单机：启动管理服务器和用户端
  ./run-demo.sh server                  # 两机：在服务器电脑启动管理端
  ./run-demo.sh client <服务器IPv4>  # 两机：在用户电脑启动用户端
EOF
}

if [[ $# -gt 2 || ( "${mode}" != "all" && "${mode}" != "server" && "${mode}" != "client" ) ]]; then
  usage >&2
  exit 2
fi
if [[ $# -eq 2 && "${mode}" != "client" ]]; then
  usage >&2
  exit 2
fi

"${project_dir}/scripts/build.sh"

cd "${project_dir}"

if [[ "${mode}" == "server" ]]; then
  exec "${server_bin}" \
    --listen-address "${listen_address}" \
    --port "${server_port}" \
    --db "${database_path}" \
    --key-file key.txt \
    --simulation-speed "${simulation_speed}"
fi

if [[ "${mode}" == "client" ]]; then
  exec "${client_bin}" --host "${server_host}" --port "${server_port}" --key-file key.txt
fi

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

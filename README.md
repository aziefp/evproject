# 电动汽车充电桩应用管理平台

课程第一阶段的完整基础系统：Linux + Qt/C++ 用户端、Qt 管理服务器、TCP Socket 通信与服务器本地 SQLite。原始要求以 [`项目说明书.docx`](项目说明书.docx) 为准；[`项目说明书简.md`](项目说明书简.md) 用于检索。

## 当前状态

- 用户端采用竖屏手机式界面，已实现手机号自动注册/登录和退出切换账号、资料头像、钱包充值、地址解析、距离找站、电桩详情、腾讯地图驾车/步行导航、预约—充电进度—充满自停—结算和订单记录；地图以移动端页面加载，并在当前 VMware + Wayland 环境自动启用已验证的兼容渲染。
- 管理端已实现管理员登录、营收趋势与电桩统计、电站新增、电桩状态筛选与故障上报/远程重启、用户搜索与冻结、订单状态筛选和代结算；列表操作均紧贴对应条目。管理界面使用橙色强调与深灰底色，与用户端形成同一品牌的深色版。
- 服务端支持多客户端 TCP、长度帧 JSON 协议、会话、心跳/空闲断开、断线重连、SQLite 事务、模拟计量，以及订单、站点、资料和冻结状态推送。
- Debug/Release 构建、自动化测试、localhost 和本机非回环 LAN 地址端到端主流程已通过；第二阶段的大数据与机器学习未实现。
- 唯一外部待验项是拿到第二台电脑后做一次物理局域网演示确认，不影响当前代码完整性。

## 构建与运行

最简单且透明的方式是运行仓库根目录的 `run-demo.sh`。它会依次执行增量编译、启动本机服务器、再启动用户端；脚本本身可直接查看，收到源码的人安装好依赖后也使用同一入口。完整机制和图形化编译、测试、Release 指导见 [`docs/08-build-test-run.md`](docs/08-build-test-run.md)。

项目固定使用已安装的 Qt 6.11.2 GCC Kit。首次构建：

```bash
./scripts/configure.sh
./scripts/build.sh
```

单机演示（同时启动管理服务器和一个用户端）：

```bash
./run-demo.sh
```

默认管理员：`admin / 123456`。登录页示例用户 `13800138001` 可直接使用；`13800138006` 是冻结场景。地图 Key 从已忽略的 `key.txt` 读取，不会进入版本库。

两机运行时，在服务器电脑的仓库根目录启动：

```bash
./build-debug/apps/admin-server/ev-admin-server \
  --listen-address 0.0.0.0 --port 45454 \
  --db runtime/evplatform.db --key-file key.txt
```

在用户电脑启动并把地址替换为服务器局域网 IPv4：

```bash
./build-debug/apps/user-client/ev-user-client \
  --host 192.168.1.10 --port 45454 --key-file key.txt
```

运行自动化测试：

```bash
/home/uh/Qt/Tools/CMake/bin/ctest --test-dir build-debug --output-on-failure
```

真实 TCP 主链路测试需要先启动一个独立测试服务器，然后运行 `scripts/integration_smoke.py --port <端口>`；脚本会使用新模拟手机号，完成充值、一笔完整订单、退出会话和重新登录。

## 文档入口

1. [`docs/01-requirements.md`](docs/01-requirements.md)：阶段边界和验收要求。
2. [`docs/02-architecture.md`](docs/02-architecture.md)：进程、线程、部署与代码边界。
3. [`docs/03-data-model-and-states.md`](docs/03-data-model-and-states.md)：数据约束、计费和状态机。
4. [`docs/04-protocol.md`](docs/04-protocol.md)：TCP 协议和请求类型。
5. [`docs/05-environment-and-external-services.md`](docs/05-environment-and-external-services.md)：Qt、地图和两机准备。
6. [`docs/06-plan-and-acceptance.md`](docs/06-plan-and-acceptance.md)：测试范围与最终验收表。
7. [`docs/07-decisions-and-status.md`](docs/07-decisions-and-status.md)：当前状态、已知限制和关键决定。
8. [`docs/08-build-test-run.md`](docs/08-build-test-run.md)：一键演示、Qt Creator 测试和 Release 编译。

文档只维护影响范围、架构、协议、数据、部署和验收的关键信息；实现变化后直接修订权威内容并清理过时描述，不积累过程日志。

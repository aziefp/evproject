# 系统架构

更新时间：2026-09-03

## 1. 部署与进程

正式模式是两机部署：电脑 A 运行 `ev-user-client`，通过可配置 LAN IP/端口连接电脑 B 的 `ev-admin-server`；SQLite 仅在电脑 B。服务器默认监听 `0.0.0.0:45454`，用户端默认连接 `127.0.0.1:45454`，因此同一套程序也能单机调试。

```text
用户端 Qt Widgets                      管理服务器 Qt Widgets
找站/钱包/订单/QWebEngineView  ─TCP─►  管理 GUI + QTcpServer + 业务服务
        │                                      │
        └──────── HTTPS 腾讯路线页             ├─ HTTPS 腾讯地址解析
                                               └─ SQLite
```

地址解析由服务器代理，用户端不直接访问数据库。路线导航使用用户端 `QWebEngineView` 打开腾讯 URI API；地图失败不影响找站、预约和充电业务。

## 2. 线程与事件模型

- GUI 主线程只负责管理页面、图表和输入，不执行 SQL 或 Socket 读写。
- `ServerWorker` 移入独立 `QThread`，在该线程内拥有 `QTcpServer`、所有客户端 Socket、唯一 SQLite 连接、地图网络管理器和充电定时器。
- 数据量小且 SQL 短，网络、地图和计时全部使用 Qt 异步事件；单工作线程同时保证数据库连接不跨线程、业务写入串行和 GUI 不冻结。
- 用户端使用非阻塞 `QTcpSocket`、15 秒心跳和有上限的自动重连；重连后自动重新登录并恢复活动订单。
- 订单计量由服务器每秒只向同一用户的会话推送；用户端只更新当前数值，不刷新整页或抢占当前标签。预约、停止、结算、电站新增和电桩重启另发站点失效事件，由客户端按需重新查询。

这一结构满足课程的多线程要求，同时避免为小型 SQLite 演示系统引入线程池和跨线程连接复杂度。

## 3. 代码边界

```text
apps/user-client       用户 GUI、连接管理、地图对话框
apps/admin-server      管理 GUI、网络服务、业务与 SQLite
libs/common            两端共用的长度帧 JSON 协议
tests                   协议和数据库业务测试
scripts                 构建、单机启动、地图与端到端检查
```

用户端只发送业务请求和显示服务端结果；订单、电桩、金额和余额的权威修改全部在服务器事务中完成。当前规模不额外拆分领域库与持久化库，避免产生没有验收价值的工程层级。

## 4. 配置方式

当前可执行程序以命令行参数作为权威配置入口：

- 服务器：`--listen-address`、`--port`、`--db`、`--key-file`、`--simulation-speed`。
- 用户端：`--host`、`--port`、`--key-file`。
- 用户端诊断：`--map-test` 可不连接服务器直接打开固定路线测试页。

用户端在“VMware 虚拟机 + Wayland 会话”组合下，会在创建 `QApplication` 前自动改用 XWayland/XCB，并给 Chromium 加上 `--disable-gpu`，规避已复现的 WebEngine 合成失败。其他环境保持 Qt 默认渲染；可用 `EV_WEBENGINE_COMPAT=0` 关闭自动判定，或用 `EV_WEBENGINE_SOFTWARE=1` 手动强制兼容模式。

`config/default.json` 和 `config/local.example.json` 仅保存可读的默认值/本地配置示例，当前程序不自动加载它们。命令行方式便于 Qt Creator、单机脚本和两机演示使用同一代码路径。

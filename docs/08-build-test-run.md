# 构建、测试与运行指南

更新时间：2026-09-08

## 1. 推荐入口：源码内的一键演示

仓库根目录的 `run-demo.sh` 是唯一的一键演示入口。它是普通文本脚本，可直接打开检查，不会安装系统应用或隐藏后台服务。

执行顺序固定且很短：

1. 调用 `scripts/build.sh`；若尚未配置则继续调用 `scripts/configure.sh`，已有构建只做快速增量编译。
2. 启动 `ev-admin-server`，监听 `0.0.0.0:45454`，数据库明确指定为仓库内的 `runtime/evplatform.db`。
3. 等待一秒，确认服务器仍在运行。
4. 启动 `ev-user-client`，连接 `127.0.0.1:45454`。
5. 用户端关闭后，脚本结束本次服务器进程；没有常驻后台服务。

在文件管理器中可右键 `run-demo.sh`，选择“作为程序运行”；若当前文件管理器只会打开脚本，则在项目目录打开终端，执行：

```bash
./run-demo.sh
```

管理员为 `admin / 123456`；普通演示用户为 `13800138001`；冻结场景用户为 `13800138006`。建议依次查看定位、站点详情、驾车/步行导航、充值、预约、充电、停止和结算，再回管理端查看订单与统计变化。

若只想确认地图渲染，可在 Qt Creator 中把用户端运行参数暂时改为：

```text
--map-test --key-file key.txt
```

它不启动服务器、不修改数据库，只显示一条固定测试路线。当前 VMware + Wayland 会自动使用兼容渲染，无需额外填写环境变量。

收到源码的其他人也使用同一入口，但电脑需先具备 Linux、Qt 6.11（含 Widgets、Network、SQL/SQLite、Charts、WebEngineWidgets）、CMake、Ninja 和 C++ 编译器。脚本会优先发现用户目录下的 Qt Online Installer 安装，也可通过 `QT_DIR` 指定 Qt Kit；地图功能还需自行准备被忽略的 `key.txt`。

## 2. 数据存放位置

正常单机演示和按本文参数运行时，所有持久数据都在服务器电脑的：

```text
<项目目录>/runtime/evplatform.db
```

当前开发机对应为 `/home/uh/evproject/runtime/evplatform.db`。这是服务器电脑上的 SQLite 文件。用户端不保存或直连数据库，用户、钱包、电站、电桩、订单和管理员数据都由服务器读写。服务器运行期间 SQLite 会按需在同目录创建 `evplatform.db-wal` 和 `evplatform.db-shm`；关闭后它们可能消失，也可能以空 WAL/缓存状态保留，都是主数据库的正常辅助文件。程序运行时不要单独移动或删除这三个文件。

数据库路径来自服务器参数 `--db runtime/evplatform.db`。相对路径以程序工作目录为准，因此 Qt Creator 的工作目录必须设置为该电脑的项目根目录。若直接传入绝对路径或其他 `--db` 参数，数据就保存在指定位置。

自动化数据库测试使用系统临时目录中的独立数据库，测试结束后自动删除，不修改 `runtime/evplatform.db`。端到端测试也应显式使用 `/tmp` 下的测试数据库，避免污染演示数据。

首次创建空数据库时，服务器自动生成 6 个电站、32 个电桩、8 个用户、60 笔历史订单及少量活动/故障场景。之后运行会复用现有文件，因此手工测试数据能够跨重启保留。数据库和运行目录已被 Git 忽略，不会随源码提交。

## 3. Qt Creator 编译与自动化测试

1. 在 Qt Creator 中打开仓库根目录的 `CMakeLists.txt`。
2. 选择 `Desktop Qt 6.11.2 GCC 64bit` Kit 并配置项目。
3. 在“项目 → 构建设置”选择 `Debug`，再选择“构建 → 构建项目”。
4. 打开“测试”视图；若列表为空，右键选择“重新扫描测试”。
5. 运行全部测试；`ProtocolTest` 和 `DatabaseTest` 应全部通过。

若从 Qt Creator 分别启动两端，两者工作目录都设为当前电脑的项目根目录。单机参数为：

```text
ev-admin-server:
--listen-address 0.0.0.0 --port 45454 --db runtime/evplatform.db --key-file key.txt --simulation-speed 60

ev-user-client:
--host 127.0.0.1 --port 45454 --key-file key.txt
```

## 4. 两台电脑局域网测试

架构始终保留两机模式：服务器电脑 B 运行管理端、TCP 服务和 SQLite，用户电脑 A 只运行用户端。单机测试仅把 A 的服务器地址换成 `127.0.0.1`，协议和业务代码不变。

### 4.1 测试前准备

1. 两台 Linux 电脑都使用同一版本源码，并已按第 3 节用 Qt Creator 编译。
2. 两机连入同一个可相互访问的局域网。如使用虚拟机，两台虚拟机应选同一类网络模式，并确保没有启用客户端隔离。
3. 两机均需安装项目所需 Qt 模块；腾讯地图还需互联网。将各自的 `key.txt` 放在项目根目录，不要通过 Git 传递 Key。
4. 在电脑 B 的 Ubuntu“设置 → 网络 → 当前连接的齿轮”中记下 IPv4 地址。局域网地址可能在重连或重启后改变，每次测试前重新确认。

### 4.2 先启动电脑 B（管理服务器）

1. 在 Qt Creator 中选择 `ev-admin-server` 运行目标。
2. 把工作目录设为电脑 B 的项目根目录，运行参数设为：

   ```text
   --listen-address 0.0.0.0 --port 45454 --db runtime/evplatform.db --key-file key.txt --simulation-speed 60
   ```

3. 运行后登录管理端，确认窗口状态显示“服务器正在监听 `0.0.0.0:45454`”。数据库只在电脑 B 上生成和更新。
4. 只有当电脑 A 连接失败且 B 已启用 UFW 时，才在 B 的项目目录打开终端查看并放行项目端口：

   ```bash
   sudo ufw status
   sudo ufw allow 45454/tcp
   ```

   请勿为测试直接关闭防火墙。若之后不再需要此规则，可执行 `sudo ufw delete allow 45454/tcp`。

### 4.3 再启动电脑 A（用户端）

1. 在 Qt Creator 中选择 `ev-user-client` 运行目标。
2. 把工作目录设为电脑 A 的项目根目录，把 `<B的IPv4>` 替换为上一步记录的数字地址：

   ```text
   --host <B的IPv4> --port 45454 --key-file key.txt
   ```

3. 运行后先确认用户端显示“已连接 `<B的IPv4>:45454`”，然后登录。电脑 B 也应提示有客户端连入。

### 4.4 最终联调顺序

1. A 用一个新手机号登录、充值，完成定位、距离排序、站点详情和驾车/步行导航。
2. A 选择空闲桩，完成预约、开始充电，观察进度条和金额增长。
3. B 确认电桩状态、订单和统计在几秒内同步，并执行一次故障桩重启和用户冻结/解冻。
4. A 手动停止或等待充满自动停止，完成结算；两端核对电桩恢复空闲、钱包扣款和营收变化。
5. 关闭后重启 B，确认用户、余额和历史订单仍在，证明 SQLite 持久化生效。

### 4.5 连接失败时快速定位

| 现象 | 优先检查 |
|---|---|
| “拒绝连接” | B 的服务器是否已启动，两端端口是否都为 `45454` |
| 一直超时 | B 的 IPv4 是否写对，UFW 是否放行，路由器/虚拟机是否隔离两机 |
| 已连接但地图不工作 | 两机的 `key.txt`、网络访问和 WebEngine 模块；可用第 1 节的 `--map-test` 单独检查 |
| 看到了意外的演示数据 | B 的 Qt Creator 工作目录与 `--db` 路径，确认打开的是 B 上的数据库 |
| 一端功能与另一端不一致 | 两机是否使用同一 Git 提交并重新编译 |

终端备用启动方式（均从各自项目根目录执行）：

```bash
# 电脑 B
./build-debug/apps/admin-server/ev-admin-server --listen-address 0.0.0.0 --port 45454 --db runtime/evplatform.db --key-file key.txt --simulation-speed 60

# 电脑 A，必须替换地址
./build-debug/apps/user-client/ev-user-client --host <B的IPv4> --port 45454 --key-file key.txt
```

2026-09-08 已在开发机上通过非回环局域网 IPv4 完成真实 TCP 联调，覆盖登录、充值幂等、地址解析、找站、预约、充电计量、停止、结算、历史和多客户端推送。这证明代码的非本机监听和连接路径可用；但真正的两台物理电脑仍需按本节做一次现场验收，因为防火墙、路由器隔离和目标机 Qt 运行环境不是程序能单独保证的条件。

## 5. Release 便携包与最终发布

在 Qt Creator 的“项目 → 构建设置”中新增或克隆构建配置，将构建类型设为 `Release`，使用独立构建目录（例如 `/home/uh/evproject/build-qtcreator-release`），运行 CMake 后构建项目。主要产物为：

```text
apps/admin-server/ev-admin-server
apps/user-client/ev-user-client
```

仓库提供可复现的便携包生成脚本：

```bash
./scripts/package-portable.sh
```

脚本会重新配置并编译 Release，通过 Qt 的 CMake 部署机制收集两个程序所需动态库、插件、`QtWebEngineProcess`、Chromium 资源和语言文件，再拷贝已忽略的本机 `key.txt`。成品固定在：

```text
<项目目录>/便捷运行包/BIT比特充电-Linux-x86_64/
```

成品目录内双击“运行演示.sh”即可。它只启动已打包的管理端和用户端，不再编译、不安装系统文件，关闭用户端后自动结束本次服务器。演示数据独立保存在包内 `runtime/evplatform.db`，不会修改源码目录的数据库。“检查完整性.sh”可复查关键文件、相对路径配置和当前电脑的动态库解析情况。

便携包已携带项目私有的 Qt 依赖，所以收包电脑不需要 Qt Creator、CMake、Ninja、编译器或单独安装 Qt。它仍不是跨操作系统容器：目标机必须是 glibc 2.34 或更高版本的 x86_64 Linux 图形桌面（如 Ubuntu 22.04/24.04），并由系统提供 Linux 内核、X11/XCB 基础库、显卡驱动/Mesa、字体和声音等基础组件。地图还需互联网。当前成品已在 Ubuntu 24.04 验证；正式分发前仍应在目标电脑上完整启动一次，这是 Linux 动态发布的平台边界，不是缺少 Qt 打包。

终端编译 Release 的备用命令：

```bash
BUILD_DIR=/home/uh/evproject/build-release BUILD_TYPE=Release ./scripts/configure.sh
BUILD_DIR=/home/uh/evproject/build-release ./scripts/build.sh
```

自动化测试备用命令：

```bash
/home/uh/Qt/Tools/CMake/bin/ctest --test-dir build-debug --output-on-failure
```

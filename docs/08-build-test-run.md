# 构建、测试与运行指南

更新时间：2026-09-03

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

正常单机演示和按本文参数运行时，所有持久数据都在：

```text
/home/uh/evproject/runtime/evplatform.db
```

这是服务器电脑上的 SQLite 文件。用户端不保存或直连数据库，用户、钱包、电站、电桩、订单和管理员数据都由服务器读写。服务器运行期间 SQLite 会按需在同目录创建 `evplatform.db-wal` 和 `evplatform.db-shm`；关闭后它们可能消失，也可能以空 WAL/缓存状态保留，都是主数据库的正常辅助文件。程序运行时不要单独移动或删除这三个文件。

数据库路径来自服务器参数 `--db runtime/evplatform.db`。相对路径以程序工作目录为准，因此 Qt Creator 的工作目录必须设置为 `/home/uh/evproject`。若直接传入绝对路径或其他 `--db` 参数，数据就保存在指定位置。

自动化数据库测试使用系统临时目录中的独立数据库，测试结束后自动删除，不修改 `runtime/evplatform.db`。端到端测试也应显式使用 `/tmp` 下的测试数据库，避免污染演示数据。

首次创建空数据库时，服务器自动生成 6 个电站、32 个电桩、8 个用户、60 笔历史订单及少量活动/故障场景。之后运行会复用现有文件，因此手工测试数据能够跨重启保留。数据库和运行目录已被 Git 忽略，不会随源码提交。

## 3. Qt Creator 编译与自动化测试

1. 在 Qt Creator 中打开仓库根目录的 `CMakeLists.txt`。
2. 选择 `Desktop Qt 6.11.2 GCC 64bit` Kit 并配置项目。
3. 在“项目 → 构建设置”选择 `Debug`，再选择“构建 → 构建项目”。
4. 打开“测试”视图；若列表为空，右键选择“重新扫描测试”。
5. 运行全部测试；`ProtocolTest` 和 `DatabaseTest` 应全部通过。

若从 Qt Creator 分别启动两端，两者工作目录都设为 `/home/uh/evproject`。参数为：

```text
ev-admin-server:
--listen-address 0.0.0.0 --port 45454 --db runtime/evplatform.db --key-file key.txt --simulation-speed 60

ev-user-client:
--host 127.0.0.1 --port 45454 --key-file key.txt
```

## 4. Release 编译与最终发布

在 Qt Creator 的“项目 → 构建设置”中新增或克隆构建配置，将构建类型设为 `Release`，使用独立构建目录（例如 `/home/uh/evproject/build-qtcreator-release`），运行 CMake 后构建项目。主要产物为：

```text
apps/admin-server/ev-admin-server
apps/user-client/ev-user-client
```

Release 可执行文件适合当前开发机上的最终演示，但不能替代源码验收：它可能与最新源码不同步，也依赖目标机上的 Qt/WebEngine 动态库。最终给另一台电脑做“无需编译的一键运行包”时，应针对那台电脑制作包含运行库的部署包；在目标环境确定前，源码内的 `run-demo.sh` 更可解释、可复现。

终端编译 Release 的备用命令：

```bash
BUILD_DIR=/home/uh/evproject/build-release BUILD_TYPE=Release ./scripts/configure.sh
BUILD_DIR=/home/uh/evproject/build-release ./scripts/build.sh
```

自动化测试备用命令：

```bash
/home/uh/Qt/Tools/CMake/bin/ctest --test-dir build-debug --output-on-failure
```

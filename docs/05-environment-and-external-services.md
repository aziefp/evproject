# 开发环境与外部服务

更新时间：2026-09-03

## 1. 已检测工具链

当前主开发机：Ubuntu 24.04。

| 工具 | 当前值 | 路径/说明 |
|---|---|---|
| Qt | 6.11.2 | `/home/uh/Qt/6.11.2/gcc_64` |
| Qt Creator | 20.0.1 | `/home/uh/Qt/Tools/QtCreator/bin/qtcreator` |
| Creator 自带 Qt | 6.11.1 | 仅用于 Creator 本身，不作为项目 Kit |
| CMake | 3.30.5 | `/home/uh/Qt/Tools/CMake/bin/cmake` |
| 编译器 | G++ 13.3 | `/usr/bin/g++` |
| 已有模块 | Core, Widgets, Network, Sql, Test, Charts | Qt 6.11.2 Kit |
| SQLite 插件 | `libqsqlite.so` | 已安装 |
| Qt WebEngineWidgets | 已安装并验证 | WebChannel 与 Positioning 依赖完整 |

上次用系统 `PATH` 检测到 Qt 6.4.2，是 Ubuntu 系统 Qt；项目统一使用 `/home/uh/Qt/6.11.2/gcc_64`，不得混用两套 Qt 的头文件和库。

推荐 shell 配置由项目脚本完成，不要求全局修改 `PATH`。CMake 配置时显式使用：

```bash
/home/uh/Qt/Tools/CMake/bin/cmake \
  -S . -B build-debug \
  -DCMAKE_PREFIX_PATH=/home/uh/Qt/6.11.2/gcc_64 \
  -DCMAKE_BUILD_TYPE=Debug
```

## 2. WebEngine 安装与验证

当前环境已完成安装和预检。若以后重建环境，应在 `/home/uh/Qt/MaintenanceTool` 中：

1. 选择“添加或删除组件”。
2. 展开主树中的 `Qt > Qt 6.11.2`，不是 Extensions 下的 WebEngine 节点。
3. 勾选 Qt WebChannel 和 Qt Positioning 的 Desktop/GCC 64-bit 组件。
4. 完成安装后，不需要手工复制文件。

WebEngine、WebChannel 和 Positioning 的 `Sources`、`Debug Information Files` 都不是本项目所必需。

完成标志：存在目录
`/home/uh/Qt/6.11.2/gcc_64/lib/cmake/Qt6WebChannel` 和
`/home/uh/Qt/6.11.2/gcc_64/lib/cmake/Qt6Positioning`，且 `QtWebEngineProcess` 不再报告动态库缺失。Qt 官方文档确认 QWidget 项目通过 WebEngineWidgets 使用 `QWebEngineView`：<https://doc.qt.io/qt-6/qtwebenginewidgets-module.html>。

2026-09-03 在真实桌面会话对同一腾讯路线页做了 A/B 验证：默认 Wayland/GPU 模式中页面网络加载成功、DOM 完整，但 VMware 的 GBM/EGL 合成连续报错，窗口截图为黑色并会出现重影；改为 XWayland/XCB 并设置 `QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu` 后，路线、地图和控件均正确显示。因此用户端只在检测到 VMware + Wayland 时自动启用这一兼容模式，且始终保留 Chromium sandbox。Qt 官方也说明 GPU 互操作失败时自动软件回退可能失败，并给出 `--disable-gpu` 作为强制软件渲染方式：[Qt WebEngine Features](https://doc.qt.io/qt-6/qtwebengine-features.html)。

地图对话框会显示加载进度和渲染进程异常，提供“重新加载”和“在浏览器中打开”恢复动作。对话框使用独立 WebEngine Profile 和移动端 User-Agent，以 420×720 竖屏尺寸实测已加载腾讯地图移动端路线页。单独检查地图可运行：

```bash
./build-debug/apps/user-client/ev-user-client --map-test --key-file key.txt
```

兼容判定通常无需配置。排障时，`EV_WEBENGINE_COMPAT=0` 可关闭自动判定，`EV_WEBENGINE_SOFTWARE=1` 可在其他机器强制兼容模式。

## 3. 腾讯位置服务

腾讯位置服务开发 Key 已由用户准备并保存在仓库根目录的 `key.txt`（被 Git 忽略）。服务器用它调用 WebService 地址解析；用户端的 `QWebEngineView` 按腾讯 [URI 路线规划规范](https://lbs.qq.com/webApi/uriV1/uriGuide/uriWebRoute)展示驾车/步行路线。

若以后更换 Key，只替换该文件内容；程序通过 `--key-file` 指定路径。不得把密钥复制进源码、配置模板、文档或测试输出。

本地 Key 和接口权限可用以下命令复查；输出不会包含 Key：

```bash
python3 scripts/check_tencent_map.py --key-file key.txt
```

2026-09-02 实测结果：地址解析、驾车路线、步行路线和 URI 路线页全部成功；完整 TCP 联调再次通过服务器地址解析。`key.txt` 权限为 `600`。

## 4. 两机联调准备

用户后续需要提供或完成的物理环境操作：

- 第二台 Linux 电脑或虚拟机，Qt 运行库版本与部署包兼容。
- 两台电脑处于可互访的局域网，并确认服务器局域网 IPv4。
- 如 Ubuntu 防火墙启用，允许项目 TCP 端口 `45454`。
- 两机系统时间基本一致，便于日志和订单时间核对。

在两机可用前，所有功能都用 localhost 多客户端模式开发，不等待硬件环境。

## 5. 机密信息规则

- 系统密码、地图 Key、令牌和管理员明文密码不写入文档、源码、测试快照或日志。
- 需要系统提权时使用受控授权，不在普通 shell 命令中回显密码。
- 提交 `local.example.json`，不提交 `local.json`。

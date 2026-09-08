# BIT比特充电—项目运行说明

本项目包含用户端和管理服务器两个 Qt 程序。管理服务器同时负责 TCP 通信、SQLite 数据库和充电模拟；用户端通过 TCP 访问服务器，并通过 Qt WebEngine 显示腾讯地图。

建议按下列顺序选择运行方式：

1. **便捷运行包**：最快，无需编译，适合直接验收和演示。
2. **Qt Creator 构建与运行**：适合查看源码、分别启动两端。
3. **`run-demo.sh` 快速脚本**：自动配置、增量编译和启动，适合已安装 Qt 的 Linux 电脑。
4. **终端手动方式**：作为构建、运行或排错的备用方式。

## 1. 便捷运行包（推荐）

### 环境要求

- x86_64 Linux 图形桌面，推荐 Ubuntu 22.04 或 Ubuntu 24.04。
- glibc 2.34 或更高版本，以及普通 Ubuntu 桌面提供的 X11/XCB、OpenGL/Mesa、字体和声音等基础组件。
- 地图、定位和导航功能需要互联网。
- **不需要** Qt、Qt Creator、CMake、Ninja、G++ 或 Python。

便携包已包含管理端、用户端、Qt 运行库、SQLite 插件、Qt WebEngine 辅助进程与资源，以及腾讯位置服务 Key。

### 单机运行

1. 在“便捷运行包”目录中找到 `BIT比特充电-Linux-x86_64.zip`。
2. 右键压缩包，选择“提取到此处”或“解压”。不要在压缩包内直接运行。
3. 打开解压后的 `BIT比特充电-Linux-x86_64` 文件夹。
4. 双击 `运行演示.sh`，若文件管理器询问，选择“运行”或“作为程序运行”。
5. 管理端和用户端会同时出现。关闭用户端后，脚本会结束本次启动的服务器，不会留下常驻服务。

若双击后只打开文本，在文件属性的“权限”页中启用“允许作为程序执行”，再次双击。也可在该文件夹中打开终端并运行：

```bash
bash 运行演示.sh
```

便携包的数据保存在解压目录内的 `runtime/evplatform.db`。首次运行会生成模拟数据，以后运行会继续使用它。要恢复初始数据，先关闭两端，再删除 `runtime` 中的 `evplatform.db`、`evplatform.db-wal` 和 `evplatform.db-shm`。

## 2. Qt Creator 构建与运行

### 环境要求

- x86_64 Linux 图形桌面，建议 Ubuntu 22.04/24.04。
- Qt Creator、CMake、Ninja 和支持 C++17 的 GCC/G++。
- `Desktop Qt 6.11.2 GCC 64-bit` Kit。
- Qt 模块：Core、Widgets、Network、Sql/SQLite、Charts、WebEngineWidgets、WebChannel、Positioning 和 Test。
- 源码根目录中的 `key.txt`：服务器用于地址解析，用户端用于地图导航。若源码包未单独提供，可从便捷运行包的解压目录复制到源码根目录。
- 地图需要互联网；两机运行时两台电脑还必须能在同一局域网内相互访问。

### 首次构建

1. 启动 Qt Creator，选择“文件 → 打开文件或项目”，打开源码根目录的 `CMakeLists.txt`。
2. 在 Kit 选择页勾选 `Desktop Qt 6.11.2 GCC 64-bit`，构建类型可选 `Debug`，然后选择“配置项目”。
3. 选择“构建 → 构建所有项目”。构建完成后应产生 `ev-admin-server` 和 `ev-user-client` 两个可执行程序。
4. 在“项目 → 运行设置”中，把两个运行目标的“工作目录”都设为源码根目录，然后按下面的单机或两机方式设置参数。

### 单机运行

1. 选择 `ev-admin-server` 运行目标，参数设为：

   ```text
   --listen-address 0.0.0.0 --port 45454 --db runtime/evplatform.db --key-file key.txt --simulation-speed 60
   ```

2. 启动管理端，确认状态栏显示服务器正在监听 `0.0.0.0:45454`。
3. 保持管理端运行，选择 `ev-user-client` 运行目标，参数设为：

   ```text
   --host 127.0.0.1 --port 45454 --key-file key.txt
   ```

4. 启动用户端。如果当前 Qt Creator 配置不允许同时运行两个目标，可用第二个 Qt Creator 窗口打开同一项目，或者用第 3 节的单机脚本方式。

单机数据库位于源码根目录的 `runtime/evplatform.db`。

### 两机运行

以电脑 B 为管理服务器，电脑 A 为用户端。两台电脑都按上述方法打开并构建同一版本源码。

1. 在 B 的 Ubuntu“设置 → 网络 → 当前连接的齿轮”中查看局域网 IPv4，例如 `192.168.1.10`。
2. 在 B 上选择 `ev-admin-server`，工作目录设为 B 的源码根目录，参数设为：

   ```text
   --listen-address 0.0.0.0 --port 45454 --db runtime/evplatform.db --key-file key.txt --simulation-speed 60
   ```

3. 启动 B 的管理端，确认已监听。所有 SQLite 数据只保存在 B。
4. 在 A 上选择 `ev-user-client`，工作目录设为 A 的源码根目录，把参数中的地址替换为 B 的 IPv4：

   ```text
   --host 192.168.1.10 --port 45454 --key-file key.txt
   ```

5. 启动 A 的用户端，确认其显示已连接 B 的地址。

如果 A 连接超时，先确认两机在同一局域网、IPv4 没有变化、B 已启动服务器。仅当 B 已启用 UFW 时，在 B 上放行项目端口：

```bash
sudo ufw allow 45454/tcp
```

## 3. `run-demo.sh` 快速脚本

### 环境要求

- Bash 和常规 Linux 命令行工具。
- Qt 6.11.2 GCC 64-bit，模块与第 2 节相同。
- CMake、Ninja、GCC/G++ 和完整源码。
- 源码根目录内存在 `key.txt`。
- 脚本会优先自动发现 `~/Qt/6.11.2/gcc_64`；其他安装位置可用 `QT_DIR` 指定。

### 单机运行

在文件管理器中右键源码根目录的 `run-demo.sh`，选择“作为程序运行”。脚本会依次完成：

1. 首次自动配置 Debug 构建，以后只做增量编译。
2. 启动管理服务器，监听 `0.0.0.0:45454`。
3. 启动用户端，连接 `127.0.0.1:45454`。
4. 关闭用户端后自动结束本次服务器。

终端中的等价命令是：

```bash
./run-demo.sh
```

### 两机运行

两台电脑都需要完整源码和构建环境。先查看服务器电脑 B 的局域网 IPv4，然后依次执行：

电脑 B：

```bash
./run-demo.sh server
```

电脑 A，将示例地址替换为 B 的实际 IPv4：

```bash
./run-demo.sh client 192.168.1.10
```

`server` 模式只启动管理服务器，`client` 模式只启动用户端；两种模式在启动前都会执行必要的增量编译。若 B 启用了 UFW，同样需要允许 `45454/tcp`。

## 4. 终端手动构建与运行（备用）

以下命令都在源码根目录执行。环境要求与第 2、3 节相同。

配置与构建：

```bash
./scripts/configure.sh
./scripts/build.sh
```

单机启动：先在一个终端启动管理服务器，再在另一个终端启动用户端。

```bash
./build-debug/apps/admin-server/ev-admin-server --listen-address 0.0.0.0 --port 45454 --db runtime/evplatform.db --key-file key.txt --simulation-speed 60
```

```bash
./build-debug/apps/user-client/ev-user-client --host 127.0.0.1 --port 45454 --key-file key.txt
```

两机运行时，在 B 上使用上面的服务器命令，在 A 上把用户端的 `127.0.0.1` 替换为 B 的局域网 IPv4。

## 5. 登录、数据和快速检查

- 管理员账号：`admin`，密码：`123456`。
- 普通演示用户：`13800138001`；登录页输入手机号即可登录，新手机号会自动创建模拟账号。
- 冻结场景用户：`13800138006`。
- 源码模式的数据库为 `<源码根目录>/runtime/evplatform.db`；两机时只位于服务器电脑 B。
- 正常验收顺序可为：登录→充值→搜索附近→站点详情与导航→预约→开始充电→观察进度→停止或充满自停→结算→在管理端查看同步结果。

## 6. 常见问题

| 现象 | 检查方向 |
|---|---|
| 用户端提示拒绝连接 | 管理服务器是否已启动，两端是否都使用端口 `45454` |
| 两机连接超时 | 服务器 IPv4 是否正确，两机是否在同一局域网，UFW 是否放行端口 |
| 地图空白或不能定位 | 检查 `key.txt`、互联网、Qt WebEngine；地图窗口内也可重新加载或改用外部浏览器 |
| 启动时提示端口被占用 | 关闭先前启动的管理端，或者让两端同时改用另一端口 |
| 看到不同的演示数据 | 检查管理端的工作目录和 `--db` 参数 |

## 7. 项目结构概要

```text
apps/user-client        用户端界面、TCP 客户端和腾讯地图
apps/admin-server       管理端界面、TCP 服务器、业务和 SQLite
libs/common             两端共用的长度帧 JSON 协议
tests                   协议与数据库自动化测试
scripts                 构建、启动、打包和联调工具
runtime                 运行时 SQLite 数据目录
```

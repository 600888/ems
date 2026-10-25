# ems
储能EMS能量管理系统

设计文档入口：[EMS 设计文档](website/docs/index.mdx)。目标架构包含采集与上送、实时数据库、存储、策略及 C++17 + Drogon Web 五类进程，前端为 Vue 3。[实时数据库进程 ems-rtdb](website/docs/processes/data/design.md)常驻并按配置选择 Redis、共享内存或其他合格后端，业务只使用统一 RealtimeClient 接口，连接与直接访问封装在公共组件内部。下面的构建说明与现有实现状态分开维护。

在线文档：[EMS 设计文档](https://600888.github.io/ems/)。合并到 `main` 的文档变更会由 GitHub Actions 自动构建并发布到 GitHub Pages。

实时数据库源码、公共头和测试已统一到 `rtdb`，按动态库交付：公共头 + `ems::rtdb`，共享内存使用系统 Boost.Interprocess（本机 Boost 1.89），Redis 使用仓库内 redis-plus-plus。记录接口、原有业务门面、采集分发器接入、配置/常驻入口和验证范围见[实时数据库实现说明](backend/src/rtdb/README.md)；来源授权和完整管理 RPC 尚待接入。

文档站点独立放在 `website/`，使用相同的 Rspress 2 主题，需要 Node.js 22.12+：

```bash
cd website
npm ci
npm run dev
```

打开终端显示的本地地址即可阅读，修改 `website/docs/` 后自动刷新。在 `website/` 内运行
`npm run build` 生成 `website/doc_build/`，`npm run preview` 预览构建产物。导航、主题和部署路径说明见
[文档维护与 Rspress](website/docs/guide/documentation.md)。

设备点表使用 `ems_simulate` 的四遥 XLSX 格式，空白模板、协议示例和事务入库说明统一放在根目录 [data/](data/README.md)。公共模型的 Modbus CSV/XLSX 格式见 [点表导入说明](website/docs/architecture/point-table-import.md)。

设备点表参考 `ems_simulate` 的 XLSX 格式，空白模板和协议示例统一放在根目录 [data/](data/README.md)。当前公共模块支持的 Modbus CSV 格式见 [点表导入说明](website/docs/architecture/point-table-import.md)。

## 依赖安装

项目使用 redis-plus-plus 作为 Redis 客户端库，已集成在 `backend/third_party/` 目录下。
DLT645 默认启用，需要 **Boost 1.83+ 头文件**；协议栈源码已集成在 `backend/third_party/dlt645/`。
可通过 `-DBOOST_ROOT=<Boost 根目录>` 指定头文件，或用 `-DEMS_BUILD_DLT645=OFF` 关闭该插件。
发行版提供的 Boost 版本较旧时，需要单独准备 1.83+；仅使用头文件，无需编译 Boost 库。

### Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install -y pkg-config libhiredis-dev libmodbus-dev
```

### CentOS/RHEL

```bash
sudo yum install pkg-config hiredis-devel
```

### macOS

```bash
brew install pkg-config hiredis
```

## 构建项目

```bash
cd backend
mkdir -p build && cd build
cmake ..
make
```

## Windows 构建

准备 CMake 3.21+、支持 C++17 的编译器，以及同架构、同工具链编译的
`hiredis >= 1.0`（Windows 缺少安装包时默认下载并使用当前工具链编译固定的 1.3.0）。
Modbus 协议插件需要 `libmodbus >= 3.1.6`；Windows 缺少安装库时默认下载并使用当前工具链
编译固定的 3.1.10，共享库随插件部署。旧处理器仍需预装 libmodbus 和 pkg-config。
`redis-plus-plus`、GoogleTest 和 IEC104 C 栈使用仓库内版本。
DLT645 还需要 Boost 1.83+；其头文件可跨 MSVC / MinGW 使用。
MSVC 构建需要 Ninja，并在 **x64 Native Tools Command Prompt / Developer PowerShell** 中执行；
MinGW 构建需要将 MinGW-w64 的 `bin` 目录加入 `PATH`。

### MSVC

假设依赖安装在 `C:/deps/msvc-x64`，其下包含 `include`、`lib` 和运行时 DLL：

```powershell
cd backend
cmake --preset windows-msvc -DCMAKE_PREFIX_PATH=C:/deps/msvc-x64 -DBOOST_ROOT=C:/deps/boost
cmake --build --preset windows-msvc
ctest --preset windows-msvc

# 同一构建目录也支持 Debug；依赖应提供匹配的调试运行库。
cmake --build --preset windows-msvc --config Debug
ctest --preset windows-msvc -C Debug
```

### MinGW-w64

```powershell
cd backend
cmake --preset windows-mingw -DCMAKE_PREFIX_PATH=C:/deps/mingw-x64 -DBOOST_ROOT=C:/deps/boost
cmake --build --preset windows-mingw
ctest --preset windows-mingw
```

也可以在仓库根目录使用 PowerShell 脚本完成配置、编译和测试：

```powershell
./scripts/build_windows.ps1 -Toolchain msvc -PrefixPath C:/deps/msvc-x64 -CMakeArgs @('-DBOOST_ROOT=C:/deps/boost') -Test
./scripts/build_windows.ps1 -Toolchain mingw -PrefixPath C:/deps/mingw-x64 -CMakeArgs @('-DBOOST_ROOT=C:/deps/boost') -Test
```

依赖不在标准安装布局时，通过 `-DLIBMODBUS_INCLUDE_DIR=...`、`-DLIBMODBUS_LIBRARY=...`、
`-DHIREDIS_HEADER=...`（包含 `hiredis/` 的父目录）、`-DHIREDIS_LIB=...` 显式指定。
脚本通过 `-CMakeArgs @('-DLIBMODBUS_INCLUDE_DIR=...', '-DLIBMODBUS_LIBRARY=...')` 传入这些参数。
使用动态依赖时，将依赖 DLL 放入程序/插件目录或加入 `PATH`；
hiredis 的 CMake 包提供 DLL 路径时，采集示例会自动复制该 DLL。
协议插件额外的 DLL 可通过 `-DEMS_PROTOCOL_RUNTIME_DEPENDENCIES='C:/deps/modbus.dll;...'` 部署。

使用 VSCode CMake Tools 时，将源码目录设为 `${workspaceFolder}/backend`，选择
`windows-msvc` 或 `windows-mingw` Configure Preset 后使用齿轮构建。首次自动准备依赖需要联网，
后续复用构建目录内的下载和编译结果。若外部依赖目录已删除，先执行
`CMake: Delete Cache and Reconfigure`，清除旧的 `CMAKE_PREFIX_PATH` 和 FetchContent 源码覆盖路径。
可设置 `EMS_FETCH_HIREDIS=OFF` 强制使用预装 hiredis；离线构建可用
`FETCHCONTENT_SOURCE_DIR_EMS_HIREDIS` 指定已有官方源码目录。
libmodbus 同样支持 `EMS_FETCH_LIBMODBUS=OFF` 强制使用预装库，或用
`FETCHCONTENT_SOURCE_DIR_EMS_LIBMODBUS` 指定离线的官方 3.1.10 源码目录。
Windows 开启 Web 测试且未安装 SQLite 时，会自动编译固定的 SQLite 3.50.2；
该静态库仅进入 `tests/archive/`。可用 `EMS_FETCH_SQLITE=OFF` 强制使用预装 SQLite，
或用 `FETCHCONTENT_SOURCE_DIR_EMS_SQLITE` 指定离线的 amalgamation 源码目录。

Windows 默认构建 Modbus / IEC104 / DLT645 动态插件、独立 handler 层、采集引擎、数据库和实时存储库，以及公共模块和存储测试。
三个协议的统一处理器、目录和采集接入见 [协议处理器接入](website/docs/reference/protocol/handlers.md)。
DLT645 的配置、测点编码和模拟器互通说明见 [DLT645 动态库接入](website/docs/reference/protocol/dlt645.md)。

DLT698 默认启用，复用统一插件 ABI、Handler 和采集通道，支持 TCP/串口采集及只读上送。
配置和 OAD 测点编码见 [DLT698 动态库接入](website/docs/reference/protocol/dlt698.md)，
可通过 `-DEMS_BUILD_DLT698=OFF` 关闭。协议栈及 Asio 均使用仓库源码，无需额外安装。
旧版 libmodbuspp 处理器、对应的四个示例和设备处理器测试由 `EMS_BUILD_LEGACY_MODBUS` 控制：
Linux 默认开启，Windows 默认关闭；这些旧目标仍依赖上游的 pkg-config 构建入口。
`BUILD_TESTS=OFF` 可关闭测试，`BUILD_EXAMPLES=OFF` 可关闭示例。
`EMS_BUILD_WEB`、`EMS_BUILD_COLLECTOR` 默认开启，分别生成 `ems-web` 和 `ems-collector`；
两者独立于示例开关，可用 `-DEMS_BUILD_WEB=OFF`、`-DEMS_BUILD_COLLECTOR=OFF` 分别关闭。
Web 还需要同工具链的 Trantor、JsonCpp 和 zlib；Windows 可先在对应编译器终端执行
`./scripts/setup_web_dependencies.ps1 -Toolchain msvc`。CMake 会自动查找
`backend/build/web-deps-msvc/install`；MinGW 使用 `-Toolchain mingw` 和对应的独立依赖目录。
自定义依赖位置可通过 `EMS_WEB_DEPENDENCY_PREFIX` 或 `CMAKE_PREFIX_PATH` 指定。
详细说明见 [Web 构建与运行](website/docs/processes/web/running.md) 和
[采集进程构建与运行](website/docs/processes/collector/running.md)。
MSVC 可执行文件及其运行依赖 DLL 位于 `backend/build/windows-msvc/bin/<配置>/`，
协议插件位于 `lib/<配置>/`，配置文件位于构建根目录的 `conf/`。

构建目录中的运行交付物、测试和示例按用途分离：

| 目录（相对构建根目录） | 内容 |
| --- | --- |
| `bin/` | 正式可执行程序及其 Windows 运行依赖 DLL |
| `lib/` | 协议插件及其依赖、Linux 共享库 |
| `archive/` | 正式目标的静态库、Windows 导入库和链接中间文件 |
| `tests/bin/` | 测试可执行程序及其 Windows 运行依赖 DLL |
| `tests/lib/` | 测试插件夹具和共享库 |
| `tests/archive/` | GoogleTest / GoogleMock 等测试静态库和导入库 |
| `examples/bin/` | 示例可执行程序（包括 `ems_point_import`）及其 Windows 运行依赖 DLL |
| `examples/lib/`、`examples/archive/` | 示例的共享库和链接产物（如有） |
| `symbols/`、`tests/symbols/`、`examples/symbols/` | 对应目标的 Windows 调试符号 |

MSVC 等多配置生成器会在上述各目录下追加 `<配置>/`（如 `Release/`、`Debug/`）；
MinGW 和 Linux 单配置构建直接使用上述目录。测试继续通过 `ctest --preset ...` 运行。
Windows 构建时会将已声明的运行依赖 DLL 复制到对应 EXE 所在目录。
已有构建目录切换布局前，应移走旧 `bin/`、`lib/` 中的产物再重新构建，避免残留文件混入交付目录。

## Linux ARM (ARM64) 构建

### 1) 板端原生构建（推荐）

在 Linux ARM64 设备上执行：

```bash
./scripts/build_linux_arm.sh native Release
```

说明：该脚本默认 `BUILD_TESTS=OFF`，优先保证部署构建成功。

### 2) x86_64 主机交叉编译

先安装交叉工具链：

```bash
sudo apt-get update
sudo apt-get install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

如果需要构建 `iec104` 的 Python 绑定，再安装 ARM64 Python 开发包：

```bash
sudo dpkg --add-architecture arm64
sudo apt-get update
sudo apt-get install -y python3-dev:arm64 libpython3-dev:arm64
```

然后执行：

```bash
./scripts/build_linux_arm.sh cross Release
```

如果 Python 头文件或库不在默认路径，可以通过环境变量覆盖：

```bash
PYTHON_EXECUTABLE=/usr/bin/python3 \
PYTHON_INCLUDE_DIR=/usr/include/aarch64-linux-gnu/python3.12 \
PYTHON_LIBRARY=/usr/lib/aarch64-linux-gnu/libpython3.12.so \
./scripts/build_linux_arm.sh cross Release
```

### 3) 使用 CMake Preset

```bash
cd backend
cmake --preset linux-arm-native
cmake --build --preset linux-arm-native
```

## 代码格式化

在仓库根目录运行以下命令，Windows PowerShell、Linux 和 macOS 使用同一入口。需要 Python 3.9+ 和现有前端所需的 Node.js/npm。

```bash
# 安装固定版本工具到工作区，不依赖系统 PATH 中的 clang-format 版本
python -m pip install --target .cache/format-tools -r scripts/requirements-format.txt
npm --prefix frontend ci

# 格式化全部自有源码
python scripts/format.py

# 仅检查，不修改文件；不符合规则时返回非 0
python scripts/format.py --check

# 预览格式差异，不修改文件
python scripts/format.py --diff

# 按语言或目录处理
python scripts/format.py --scope cpp
python scripts/format.py --scope cmake
python scripts/format.py --scope web
python scripts/format.py backend/src/web
python scripts/format.py --check frontend/src
```

| 代码 | 规则与工具 | 处理范围 |
| --- | --- | --- |
| C/C++ | `.clang-format`，clang-format 22.1.8；Google 风格、4 空格、100 列 | `backend/include`、`src`、`tests`、`examples` |
| CMake | `.cmake-format.yaml`，cmakelang 0.6.13；4 空格、100 列 | 自有 `CMakeLists.txt` 和 `.cmake`，包括接入依赖的 `backend/third_party/CMakeLists.txt` |
| Vue / TypeScript / JavaScript / CSS / HTML | `.prettierrc.json`，Prettier 3.6.2；2 空格、100 列、单引号、无分号 | `frontend/src`、前端 Vite/HTML 入口、`website/theme` 和 Rspress 配置 |

工具会跳过第三方依赖、构建产物、`node_modules` 和缓存，即使显式指定这些目录也不会格式化其中的源码。SQL、业务 JSON、Markdown 文档及图片不在自动格式化范围内。格式化保留逻辑间的空行与中文注释，不替代关键逻辑注释和头文件 Doxygen 要求。

仅处理前端也可以运行 `npm --prefix frontend run format` 或 `npm --prefix frontend run format:check`。
既有 `./scripts/format.sh` 与 CMake 的 `format` / `format-check` 目标仍仅处理 C/C++，需自行准备匹配版本的 clang-format；全项目格式化推荐使用上面的 Python 入口。详细规则见 [代码格式化规范](website/docs/reference/code-format.md)。

## 头文件目录

`backend/include/` 放供外部使用的公共接口，以及通用工具头文件 `util/`；其中公共接口包括 `common/`、`db/`、`realtime/` 和协议插件 C 接口。模块内部头文件放在对应的 `backend/src/` 模块目录；第三方头文件放在 `backend/third_party/`。新增内部头文件时，不要把所在模块的目录作为公共 include 路径导出。

## C++ 标准

项目使用 C++17 标准。

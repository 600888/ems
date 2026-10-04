# ems
储能EMS能量管理系统

设计文档入口：[EMS 设计文档](docs/index.md)。目标架构包含采集与上送、存储、策略及 C++17 + Drogon Web 四类进程，前端为 Vue 3；下面的构建说明与现有实现状态分开维护。

## 依赖安装

项目使用 redis-plus-plus 作为 Redis 客户端库，已集成在 `backend/3rd/` 目录下。

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

准备 CMake 3.21+、支持 C++20 的编译器，以及同架构、同工具链编译的
`hiredis >= 1.0` 和 `libmodbus >= 3.1.6`。`redis-plus-plus`、GoogleTest 和 IEC104 C 栈使用仓库内版本。
MSVC 构建需要 Ninja，并在 **x64 Native Tools Command Prompt / Developer PowerShell** 中执行；
MinGW 构建需要将 MinGW-w64 的 `bin` 目录加入 `PATH`。

### MSVC

假设依赖安装在 `C:/deps/msvc-x64`，其下包含 `include`、`lib` 和运行时 DLL：

```powershell
cd backend
cmake --preset windows-msvc -DCMAKE_PREFIX_PATH=C:/deps/msvc-x64
cmake --build --preset windows-msvc
ctest --preset windows-msvc

# 同一构建目录也支持 Debug；依赖应提供匹配的调试运行库。
cmake --build --preset windows-msvc --config Debug
ctest --preset windows-msvc -C Debug
```

### MinGW-w64

```powershell
cd backend
cmake --preset windows-mingw -DCMAKE_PREFIX_PATH=C:/deps/mingw-x64
cmake --build --preset windows-mingw
ctest --preset windows-mingw
```

也可以在仓库根目录使用 PowerShell 脚本完成配置、编译和测试：

```powershell
./scripts/build_windows.ps1 -Toolchain msvc -PrefixPath C:/deps/msvc-x64 -Test
./scripts/build_windows.ps1 -Toolchain mingw -PrefixPath C:/deps/mingw-x64 -Test
```

依赖不在标准安装布局时，通过 `-DLIBMODBUS_INCLUDE_DIR=...`、`-DLIBMODBUS_LIBRARY=...`、
`-DHIREDIS_HEADER=...`（包含 `hiredis/` 的父目录）、`-DHIREDIS_LIB=...` 显式指定。
脚本通过 `-CMakeArgs @('-DLIBMODBUS_INCLUDE_DIR=...', '-DLIBMODBUS_LIBRARY=...')` 传入这些参数。
使用动态依赖时，将依赖 DLL 放入程序/插件目录或加入 `PATH`；
hiredis 的 CMake 包提供 DLL 路径时，采集示例会自动复制该 DLL。
协议插件额外的 DLL 可通过 `-DEMS_PROTOCOL_RUNTIME_DEPENDENCIES='C:/deps/modbus.dll;...'` 部署。

Windows 默认构建 Modbus / IEC104 动态插件、采集引擎、数据库和实时存储库，以及公共模块和存储测试。
旧版 libmodbuspp 处理器、对应的四个示例和设备处理器测试由 `EMS_BUILD_LEGACY_MODBUS` 控制：
Linux 默认开启，Windows 默认关闭；这些旧目标仍依赖上游的 pkg-config 构建入口。
`BUILD_TESTS=OFF` 可关闭测试，`BUILD_EXAMPLES=OFF` 可关闭示例。
MSVC 可执行文件和通用 DLL 位于 `backend/build/windows-msvc/bin/<配置>/`，
协议插件位于 `lib/<配置>/`，配置文件位于构建根目录的 `conf/`。

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

`backend/` 下的 C/C++ 代码统一使用 clang-format（基于 Google 风格，缩进 4 空格、列宽 100）。规则见根目录 `.clang-format`，详细说明见 [代码格式化规范](docs/reference/code-format.md)。

```bash
# 安装固定版本（Windows / macOS / Linux 通用）
python -m pip install clang-format==22.1.8

# 格式化 / 检查
./scripts/format.sh            # 就地格式化
./scripts/format.sh --check    # 仅检查，CI 用
```

也可以在 `backend/build` 下使用 CMake 目标：`make format`、`make format-check`。

`backend/3rd/` 下的第三方代码不参与格式化。

CMake 文件按功能块分隔空行，统一 4 空格缩进和 100 列，规则见 `.cmake-format.yaml`。
可使用 `cmake-format -i -c .cmake-format.yaml -- <自有 CMake 文件>` 格式化；
不要对 `backend/3rd/` 内的第三方子项目执行格式整改。

## 头文件目录

`backend/include/` 放供外部使用的公共接口，以及通用工具头文件 `util/`；其中公共接口包括 `common/`、`db/`、`realtime/` 和协议插件 C 接口。模块内部头文件放在对应的 `backend/src/` 模块目录；第三方头文件放在 `backend/3rd/`。新增内部头文件时，不要把所在模块的目录作为公共 include 路径导出。

## C++ 标准

项目使用 C++20 标准。

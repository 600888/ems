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

## 头文件目录

`backend/include/` 放供外部使用的公共接口，以及通用工具头文件 `util/`；其中公共接口包括 `common/`、`db/`、`realtime/` 和协议插件 C 接口。模块内部头文件放在对应的 `backend/src/` 模块目录；第三方头文件放在 `backend/3rd/`。新增内部头文件时，不要把所在模块的目录作为公共 include 路径导出。

## C++ 标准

项目使用 C++20 标准。

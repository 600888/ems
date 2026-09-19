# ems
储能EMS能量管理系统

设计文档入口：[EMS 设计文档](docs/index.md)。文档按系统架构、采集与上送/存储/策略/Web 进程及公共模块组织，目标自研 C++ 标准为 C++17；下面的构建说明与现有实现状态分开维护。

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

## C++ 标准

项目使用 C++20 标准。

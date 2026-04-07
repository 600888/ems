# ems
储能EMS能量管理系统

## 依赖安装

项目使用 redis-plus-plus 作为 Redis 客户端库，已集成在 `backend/3rd/` 目录下。

### Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install -y pkg-config libhiredis-dev
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

## C++ 标准

项目使用 C++20 标准。

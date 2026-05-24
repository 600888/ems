# EMS Data Models 测试

## 概述

本目录包含EMS数据模型的单元测试代码。

## 测试内容

测试覆盖以下数据模型：

- `DeviceGroup` - 设备组模型测试
- `device` - 设备模型测试
- `channel` - 通道模型测试
- `PointYC` - 遥测点模型测试
- `PointYX` - 遥信点模型测试
- `PointYK` - 遥控点模型测试
- `PointYT` - 遥调点模型测试
- `Slave` - 从机模型测试
- `PointMapping` - 测点映射模型测试

## 构建测试

### 前置条件

- CMake 3.15+
- C++ 编译器 (支持 C++17)
- nlohmann/json 3.9.0+

### 构建步骤

```bash
# 创建构建目录
mkdir build
cd build

# 配置项目
cmake ..

# 编译
make

# 运行测试
./test_models
# 或者使用 ctest
ctest
```

## 运行测试

### 方法一：直接运行测试程序

```bash
cd build/tests
./test_models
```

### 方法二：使用 CTest

```bash
cd build
ctest --output-on-failure
```

### 方法三：运行特定测试

```bash
cd build/tests
./test_models
```

## 测试输出示例

```
========================================
EMS Data Models Unit Tests
========================================

Testing DeviceGroup...
DeviceGroup JSON: {
  "id": 1,
  "code": "group_001",
  "name": "测试设备组",
  ...
}
DeviceGroup test passed!

Testing device...
device JSON: {
  "id": 1,
  "code": "device_001",
  "name": "测试设备",
  ...
}
device test passed!

...
========================================
All tests passed!
========================================
```

## 添加新测试

在 `test_models.cpp` 中添加新的测试函数：

```cpp
void test_new_model() {
    std::cout << "Testing NewModel..." << std::endl;
    
    auto model = std::make_shared<NewModel>();
    // ... 设置模型属性
    // ... 执行测试断言
    
    std::cout << "NewModel test passed!" << std::endl << std::endl;
}

int main() {
    // ... 其他测试
    
    test_new_model();  // 添加新测试调用
    
    // ...
}
```

## 调试

### 使用 GDB 调试

```bash
gdb ./test_models
(gdb) run
(gdb) bt  # 查看堆栈
(gdb) break test_new_model  # 设置断点
```

### 使用 Valgrind 检测内存泄漏

```bash
valgrind --leak-check=full ./test_models
```

## 持续集成

测试可以集成到 CI/CD 流程中：

```yaml
# .github/workflows/test.yml
name: Test
on: [push, pull_request]
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Build and Test
        run: |
          mkdir build && cd build
          cmake .. -DCMAKE_BUILD_TYPE=Release
          make
          ctest --output-on-failure
```

# Modbus Handler Tests

本目录包含 Modbus 协议处理器的单元测试。

## 测试结构

- `test_utils.h` - 测试辅助工具和测试点类型定义
- `modbus_handler_test.cpp` - 主要的测试文件
- `CMakeLists.txt` - CMake 构建配置

## 测试覆盖范围

### ModbusServerHandler 测试

1. **构造函数测试**
   - 默认构造函数
   - 带日志回调的构造函数

2. **初始化测试**
   - TCP 配置初始化
   - RTU 配置初始化

3. **启动停止测试**
   - 服务器启动和停止
   - 运行状态管理

4. **数据读写测试**
   - 保持寄存器读写 (功能码 3)
   - 输入寄存器读写 (功能码 4)
   - 线圈读写 (功能码 1/5)
   - 离散输入读写 (功能码 2)

5. **数据类型解码测试**
   - Int16 解码
   - UInt32 解码
   - Float 解码

6. **测点管理测试**
   - 添加测点
   - 读写测点值

7. **消息捕获测试**
   - 捕获发送/接收消息
   - 清除捕获消息
   - 获取平均时间统计

8. **回调测试**
   - Modbus 写入回调

### ModbusClientHandler 测试

1. **构造函数测试**
   - 默认构造函数
   - 带日志回调的构造函数

2. **初始化测试**
   - TCP 配置初始化
   - RTU 配置初始化

3. **连接管理测试**
   - 连接和断开
   - 连接状态检查

4. **批量读写测试**
   - 读取保持寄存器
   - 读取输入寄存器
   - 读取线圈
   - 读取离散输入
   - 写单个寄存器
   - 写多个寄存器
   - 写单个线圈
   - 写多个线圈

5. **异步操作测试**
   - 异步读值
   - 异步写值

6. **测点操作测试**
   - 从测点读取值
   - 向测点写入值

7. **批量寄存器读取测试**
   - 批量读取寄存器

### 通用测试

1. **ValueType 转换测试**
2. **ProtocolHandler 消息捕获测试**
3. **运行状态测试**

## 构建和运行测试

### 前提条件

- CMake >= 3.15
- C++ 编译器支持 C++17
- Google Test 框架
- libmodbus 库
- nlohmann_json 库

### 构建步骤

```bash
cd backend
mkdir -p build
cd build
cmake .. -DBUILD_TESTS=ON
make test_modbus_handler
```

### 运行测试

```bash
./tests/net/device/protocol_handler/test_modbus_handler
```

或者使用 ctest:

```bash
ctest -R test_modbus_handler -V
```

### 运行特定测试

```bash
./tests/net/protocol/modbus_handler/test_modbus_handler --gtest_filter=ModbusServerHandler.Constructor
```

### 运行特定测试套件

```bash
./tests/net/protocol/modbus_handler/test_modbus_handler --gtest_filter=ModbusServerHandler.*
```

## 测试点类型说明

测试中定义了以下测试点类型：

- `TestPoint` - 通用测点
- `TestPointYX` - 遥信测点 (数字输入)
- `TestPointYC` - 遥测测点 (模拟输入)
- `TestPointYK` - 遥控测点 (数字输出)
- `TestPointYT` - 遥调测点 (模拟输出)

## 注意事项

1. 服务器测试使用不同的端口避免冲突 (5020-5046)
2. 测试之间有适当的延迟以确保服务器完全启动
3. 所有测试都会在完成后正确清理资源
4. 测试使用实际的 Modbus 服务器和客户端以确保端到端功能正常

## 添加新测试

在 `modbus_handler_test.cpp` 文件中添加新的测试用例：

```cpp
TEST(ModbusServerHandler, NewTest) {
    ModbusServerHandler<TestPoint> handler;
    // 测试代码
}
```

## 扩展测试

可以扩展 `test_utils.h` 中的测试点类型以支持更多的数据类型和功能。

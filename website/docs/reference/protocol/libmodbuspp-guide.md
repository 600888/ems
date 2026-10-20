# libmodbuspp 使用指南

> 类型：第三方库开发参考。API 和构建示例需要在选定依赖版本上验证，不代表 EMS 已实现全部能力。业务角色、设备绑定和控制闭环以 [设备、通道与协议](../../architecture/channel-protocol.md) 为准。

## 目录

1. [概述](#概述)
2. [核心概念](#核心概念)
3. [客户端（Master）开发指南](#客户端master开发指南)
   - [串口（RTU）客户端](#串口rtu客户端)
   - [网口（TCP）客户端](#网口tcp客户端)
   - [多从机通信](#多从机通信)
4. [服务端（Server）开发指南](#服务端server开发指南)
   - [TCP服务端](#tcp服务端)
   - [RTU服务端](#rtu服务端)
   - [多从机服务端](#多从机服务端)
5. [寄存器操作详解](#寄存器操作详解)
   - [保持寄存器（Holding Registers）](#保持寄存器holding-registers)
   - [输入寄存器（Input Registers）](#输入寄存器input-registers)
   - [线圈（Coils）](#线圈coils)
   - [离散输入（Discrete Inputs）](#离散输入discrete-inputs)
6. [数据类型处理详解](#数据类型处理详解)
   - [支持的数据类型一览](#支持的数据类型一览)
   - [字节序与字序详解](#字节序与字序详解)
   - [Data 模板类详解](#data-模板类详解)
   - [各数据类型完整示例](#各数据类型完整示例)
   - [手动字节序转换](#手动字节序转换)
   - [字节序检测与自动适配](#字节序检测与自动适配)
   - [常见问题与解决方案](#常见问题与解决方案)
7. [通信方式详解](#通信方式详解)
   - [RTU串口通信](#rtu串口通信)
   - [TCP网口通信](#tcp网口通信)
8. [JSON配置文件](#json配置文件)
9. [最佳实践与常见问题](#最佳实践与常见问题)

---

## 概述

libmodbuspp 是一个基于 [libmodbus](https://libmodbus.org/) 的 C++ 封装库，提供了简洁易用的 Modbus 通信接口。该库完全采用 C++ 实现，仅依赖 libmodbus 和 libstdc++，支持跨平台运行。

### 主要特性

- **支持两种通信模式**：RTU（串口）和 TCP（网口）
- **支持客户端和服务端**：可作为 Master 客户端请求数据，或作为 Server 端响应请求
- **支持多从机**：单个服务端可实现多个从机（地址1-247）
- **丰富的数据类型支持**：支持 int、float、double 等多字节数据的透明转换
- **灵活的字节序配置**：支持 Big Endian 和 Little Endian 配置
- **JSON 配置支持**：可通过 JSON 文件简化配置管理

---

## 核心概念

### Modbus 数据模型

Modbus 协议定义了四种数据对象：

| 类型 | 说明 | 访问方式 | 功能码 |
|------|------|----------|--------|
| **线圈 (Coil)** | 单位布尔值 | 读写 | 0x01, 0x05, 0x0F |
| **离散输入 (Discrete Input)** | 单位布尔值 | 只读 | 0x02 |
| **保持寄存器 (Holding Register)** | 16位无符号整数 | 读写 | 0x03, 0x06, 0x10 |
| **输入寄存器 (Input Register)** | 16位无符号整数 | 只读 | 0x04 |

### 地址模型

libmodbuspp 支持两种地址模式：

- **数据模型地址**（默认）：寄存器从 1 开始编号
- **PDU 地址**：寄存器从 0 开始编号（符合 Modbus 协议规范）

```cpp
slave.setPduAddressing(true);  // 启用PDU地址模式（从0开始）
slave.setPduAddressing(false); // 使用数据模型地址（从1开始，默认）
```

### 关键类

| 类名 | 说明 |
|------|------|
| `Modbus::Master` | Modbus 客户端（主站），用于请求从机数据 |
| `Modbus::Slave` | 从机对象，由 Master 管理 |
| `Modbus::Server` | Modbus 服务端（从站），用于响应主站请求 |
| `Modbus::BufferedSlave` | 带缓冲的从机，由 Server 管理 |
| `Modbus::Router` | 路由器，用于连接多个网络 |
| `Modbus::Data<T, E>` | 模板类，支持多字节数据类型转换 |

---

## 客户端（Master）开发指南

### 基础框架

```cpp
#include <modbuspp.h>

using namespace Modbus;

Master mb(Net网络类型, "连接参数", "设置参数");  // 创建主站
Slave & slave = mb.addSlave(从机地址);           // 添加从机
mb.open();                                        // 打开连接
// 执行读写操作...
mb.close();                                       // 关闭连接
```

### 串口（RTU）客户端

#### 基础示例

```cpp
#include <iostream>
#include <modbuspp.h>

using namespace std;
using namespace Modbus;

int main() {
    // 创建RTU主站，连接到 /dev/ttyUSB0，波特率38400，偶校验，8位数据，1位停止
    Master mb(Rtu, "/dev/ttyUSB0", "38400E1");
    
    // 添加从机（地址33）
    Slave &slave = mb.addSlave(33);
    
    // 打开连接
    if (!mb.open()) {
        cerr << "无法打开串口连接" << endl;
        return -1;
    }
    
    // 读取保持寄存器
    uint16_t values[4];
    int count = slave.readRegisters(0, values, 4);
    
    if (count > 0) {
        cout << "成功读取 " << count << " 个寄存器" << endl;
        for (int i = 0; i < count; i++) {
            cout << "R" << i << " = " << values[i] << endl;
        }
    }
    
    mb.close();
    return 0;
}
```

#### 编译与运行

```bash
g++ -o rtu_client main.cpp $(pkg-config --cflags --libs modbuspp)
./rtu_client
```

### 网口（TCP）客户端

#### 基础示例

```cpp
#include <iostream>
#include <modbuspp.h>

using namespace std;
using namespace Modbus;

int main() {
    // 创建TCP主站，连接到 192.168.1.100:502
    Master mb(Tcp, "192.168.1.100", "502");
    
    Slave &slave = mb.addSlave(1);  // 从机地址（TCP模式下通常为1）
    
    if (!mb.open()) {
        cerr << "无法建立TCP连接" << endl;
        return -1;
    }
    
    // 读取输入寄存器
    uint16_t input[2];
    if (slave.readInputRegisters(0, input, 2) == 2) {
        cout << "温度: " << input[0] << endl;
        cout << "湿度: " << input[1] << endl;
    }
    
    mb.close();
    return 0;
}
```

#### 设置超时参数

```cpp
Master mb(Tcp, "192.168.1.100", "502");

// 设置响应超时（毫秒）
mb.setResponseTimeout(1000);

// 设置字节间超时（毫秒）
mb.setByteTimeout(500);

// 设置调试模式
mb.setDebug(true);
```

### 多从机通信

#### 管理多个从机

```cpp
#include <iostream>
#include <modbuspp.h>

using namespace Modbus;

int main() {
    Master mb(Rtu, "/dev/ttyUSB0", "38400E1");
    
    // 添加多个从机（地址1, 2, 3）
    Slave &slave1 = mb.addSlave(1);
    Slave &slave2 = mb.addSlave(2);
    Slave &slave3 = mb.addSlave(3);
    
    if (mb.open()) {
        uint16_t value;
        
        // 轮询读取各从机数据
        if (slave1.readInputRegisters(0, &value, 1) == 1) {
            cout << "从机1 - 寄存器0: " << value << endl;
        }
        
        if (slave2.readInputRegisters(0, &value, 1) == 1) {
            cout << "从机2 - 寄存器0: " << value << endl;
        }
        
        if (slave3.readInputRegisters(0, &value, 1) == 1) {
            cout << "从机3 - 寄存器0: " << value << endl;
        }
        
        mb.close();
    }
    
    return 0;
}
```

#### 动态从机管理

```cpp
// 检查从机是否存在
if (mb.hasSlave(10)) {
    Slave &slave = mb.slave(10);
    // 操作从机...
}

// 获取已添加的所有从机
const map<int, shared_ptr<Slave>>& allSlaves = mb.slaves();
for (const auto& pair : allSlaves) {
    cout << "从机地址: " << pair.first << endl;
}
```

---

## 服务端（Server）开发指南

### TCP服务端

#### 基础示例

```cpp
#include <iostream>
#include <thread>
#include <chrono>
#include <modbuspp.h>

using namespace std;
using namespace Modbus;

int main() {
    // 创建TCP服务端，监听 127.0.0.1:1502
    Server srv(Tcp, "127.0.0.1", "1502");
    
    // 添加从机（地址10）
    BufferedSlave &slave = srv.addSlave(10);
    
    // 配置数据块
    slave.setBlock(InputRegister, 8);      // 8个输入寄存器
    slave.setBlock(HoldingRegister, 2);     // 2个保持寄存器
    slave.setBlock(Coil, 1);                // 1个线圈
    
    if (!srv.open()) {
        cerr << "无法打开服务端" << endl;
        return -1;
    }
    
    // 启动服务线程
    srv.run();
    
    cout << "Modbus TCP 服务端已启动，监听端口 1502" << endl;
    cout << "按 Ctrl+C 停止服务" << endl;
    
    // 模拟数据更新
    int counter = 0;
    while (srv.isRunning()) {
        this_thread::sleep_for(chrono::seconds(1));
        
        // 更新输入寄存器（模拟传感器数据）
        uint16_t time_data[8];
        time_data[0] = counter++;           // 秒
        time_data[1] = 25;                  // 温度
        time_data[2] = 60;                  // 湿度
        slave.writeInputRegisters(1, time_data, 3);
        
        // 更新保持寄存器
        slave.writeRegister(1, counter % 100);
    }
    
    srv.terminate();
    return 0;
}
```

### RTU服务端

#### 基础示例

```cpp
#include <iostream>
#include <modbuspp.h>

using namespace Modbus;

int main() {
    // 创建RTU服务端，在 /dev/ttyUSB1 上
    Server srv(Rtu, "/dev/ttyUSB1", "9600E1");
    
    // 添加从机
    BufferedSlave &slave = srv.addSlave(1);
    
    // 配置数据块
    slave.setBlock(HoldingRegister, 10);    // 10个保持寄存器
    slave.setBlock(Coil, 16);               // 16个线圈
    
    if (!srv.open()) {
        cerr << "无法打开RTU服务端" << endl;
        return -1;
    }
    
    cout << "Modbus RTU 服务端已启动" << endl;
    
    // 主循环处理请求
    while (true) {
        // 轮询等待请求，超时100ms
        int ret = srv.poll(100);
        
        if (ret > 0) {
            cout << "处理了一个请求" << endl;
        }
    }
    
    srv.close();
    return 0;
}
```

### 多从机服务端

```cpp
#include <iostream>
#include <modbuspp.h>

using namespace Modbus;

int main() {
    Server srv(Tcp, "0.0.0.0", "1502");
    
    // 添加从机1（温度传感器）
    BufferedSlave &tempSensor = srv.addSlave(1);
    tempSensor.setBlock(InputRegister, 2);   // 温度值
    tempSensor.setBlock(HoldingRegister, 1); // 配置参数
    
    // 添加从机2（湿度传感器）
    BufferedSlave &humidSensor = srv.addSlave(2);
    humidSensor.setBlock(InputRegister, 2); // 湿度值
    humidSensor.setBlock(HoldingRegister, 1);
    
    // 添加从机3（开关控制器）
    BufferedSlave &switchCtrl = srv.addSlave(3);
    switchCtrl.setBlock(Coil, 8);           // 8个继电器
    switchCtrl.setBlock(HoldingRegister, 8); // 控制参数
    
    if (!srv.open()) {
        cerr << "无法打开服务端" << endl;
        return -1;
    }
    
    srv.run();
    
    // 使用下标访问从机
    srv[1]->writeInputRegister(1, 2500);  // 温度25.00°C
    srv[2]->writeInputRegister(1, 6500);  // 湿度65.00%
    
    while (srv.isRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}
```

---

## 寄存器操作详解

### 保持寄存器（Holding Registers）

保持寄存器是最常用的寄存器类型，支持读写操作。

#### 读取保持寄存器

```cpp
// 读取单个寄存器
uint16_t value;
slave.readRegister(0, &value);

// 读取多个寄存器
uint16_t values[10];
int count = slave.readRegisters(0, values, 10);

// 使用Data模板类读取32位浮点数
Data<float> temperature;
slave.readRegisters(0, temperature.registers().data(), 2);
cout << "温度: " << temperature.value() << "°C" << endl;
```

#### 写入保持寄存器

```cpp
// 写入单个寄存器
slave.writeRegister(0, 12345);

// 写入多个寄存器
uint16_t values[4] = {100, 200, 300, 400};
slave.writeRegisters(0, values, 4);

// 使用Data模板类写入32位浮点数
Data<float> voltage(220.5);
slave.writeRegisters(10, voltage.registers().data(), 2);
```

#### 读写组合操作

```cpp
// 写入后读取（原子操作）
uint16_t writeData[2] = {10, 20};
uint16_t readData[4];
slave.writeReadRegisters(
    0, writeData, 2,    // 从地址0写入2个寄存器
    10, readData, 4     // 从地址10读取4个寄存器
);
```

### 输入寄存器（Input Registers）

输入寄存器为只读寄存器，通常用于上传传感器数据。

```cpp
// 读取输入寄存器
uint16_t sensorData[3];
int count = slave.readInputRegisters(0, sensorData, 3);

// 在服务端写入输入寄存器（模拟外部设备）
slave.writeInputRegisters(0, sensorData, 3);

// 使用Data模板类
Data<int32_t> rawValue;
slave.readInputRegisters(0, rawValue.registers().data(), 2);
```

### 线圈（Coils）

线圈是布尔类型数据，支持读写操作。

```cpp
// 读取单个线圈
bool state;
slave.readCoil(0, state);

// 读取多个线圈
bool states[8];
slave.readCoils(0, states, 8);

// 写入单个线圈
slave.writeCoil(0, true);

// 写入多个线圈
bool newStates[4] = {true, false, true, false};
slave.writeCoils(0, newStates, 4);
```

### 离散输入（Discrete Inputs）

离散输入为只读布尔类型数据。

```cpp
// 读取单个离散输入
bool input;
slave.readDiscreteInput(0, input);

// 读取多个离散输入
bool inputs[16];
int count = slave.readDiscreteInputs(0, inputs, 16);
```

### 数据类型处理详解

libmodbuspp 提供了 `Data<T, E>` 模板类，可以方便地处理多字节数据类型。本节详细介绍各种数据类型的处理方法以及大小端序的转换问题。

#### 支持的数据类型一览

| C++ 类型 | 别名 | 寄存器数量 | 说明 | 取值范围 |
|----------|------|-----------|------|----------|
| `int16_t` | `short` | 1 | 16位有符号整数 | -32,768 ~ 32,767 |
| `uint16_t` | `unsigned short` | 1 | 16位无符号整数 | 0 ~ 65,535 |
| `int32_t` | `int` (32位) | 2 | 32位有符号整数 | -2,147,483,648 ~ 2,147,483,647 |
| `uint32_t` | `unsigned int` | 2 | 32位无符号整数 | 0 ~ 4,294,967,295 |
| `int64_t` | `long long` | 4 | 64位有符号整数 | ±9.22×10¹⁸ |
| `uint64_t` | `unsigned long long` | 4 | 64位无符号整数 | 0 ~ 1.84×10¹⁹ |
| `float` | - | 2 | 32位浮点数 | ±3.4×10³⁸ (7位精度) |
| `double` | - | 4 | 64位浮点数 | ±1.8×10³⁰⁸ (15位精度) |

---

## 字节序与字序详解

### 核心概念

在 Modbus 通信中，由于每个寄存器只有 16 位（2字节），对于大于 16 位的数据类型，需要跨越多个寄存器存储。这就涉及到一个重要问题：**字节序（Byte Order）和字序（Word Order）**。

#### 字节序（Byte Order）

字节序描述了多字节数据中**字节的排列顺序**：

- **Big Endian（大端序）**：高字节在前，低字节在后
  - 例如：32位整数 `0x12345678` 存储为 `[12][34][56][78]`
- **Little Endian（小端序）**：低字节在前，高字节在后
  - 例如：32位整数 `0x12345678` 存储为 `[78][56][34][12]`

#### 字序（Word Order）

字序描述了 16 位字（寄存器）在内存中的排列顺序。在 Modbus 协议中：

- **Big Word（大字序）**：高字在前，低字在后
  - 例如：64位数据 `[0x1234][0x5678][0x9ABC][0xDEF0]` 的高字 `0x1234` 在低地址
- **Little Word（小字序）**：低字在前，高字在后
  - 例如：64位数据 `[0xDEF0][0x9ABC][0x5678][0x1234]` 的低字 `0x1234` 在低地址

#### 四种组合模式

libmodbuspp 定义了四种字节序和字序的组合模式：

| 枚举值 | 字节序 | 字序 | 寄存器存储格式 | 典型应用 |
|--------|--------|------|---------------|----------|
| `EndianBig` / `EndianBigBig` | Big | Big | `[高字节][低字节][高字节][低字节]` | 标准 Modbus（默认） |
| `EndianBigLittle` | Big | Little | `[低字节][高字节][低字节][高字节]` | PLC兼容模式 |
| `EndianLittleBig` | Little | Big | `[高字节][低字节][高字节][低字节]` | 部分西门子设备 |
| `EndianLittle` / `EndianLittleLittle` | Little | Little | `[低字节][高字节][低字节][高字节]` | x86架构 |

#### 32位数据存储示意图

假设要存储浮点数 `25.5f`（十六进制表示为 `0x41CC0000`）：

```
寄存器0                    寄存器1
┌──────────┬──────────┐   ┌──────────┬──────────┐
│  高字节   │  低字节   │   │  高字节   │  低字节   │
│ 0x41     │ 0xCC     │   │ 0x00     │ 0x00     │
└──────────┴──────────┘   └──────────┴──────────┘

EndianBig (ABCD):     寄存器0=[41][CC]  寄存器1=[00][00]
EndianBigLittle (CDAB): 寄存器0=[CC][41]  寄存器1=[00][00]
EndianLittleBig (BADC): 寄存器0=[CC][41]  寄存器1=[00][00]
EndianLittle (DCBA):  寄存器0=[00][00]  寄存器1=[CC][41]
```

#### 64位数据存储示意图

假设要存储 `int64_t` 值 `0x0123456789ABCDEF`：

```
寄存器0       寄存器1       寄存器2       寄存器3
┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐
│ 高字节01│  │    23   │  │    45   │  │    67   │  ← 字节序（Big Endian时）
│ 低字节EF│  │    CD   │  │    AB   │  │    89   │  ← 字节序（Little Endian时）
└─────────┘  └─────────┘  └─────────┘  └─────────┘

EndianBig (ABCD):        [01][23][45][67][89][AB][CD][EF]
EndianBigLittle (CDAB):  [67][45][23][01][EF][CD][AB][89]
EndianLittleBig (BADC):  [23][01][67][45][AB][89][EF][CD]
EndianLittle (DCBA):     [EF][CD][AB][89][67][45][23][01]
```

---

### 常见设备的字节序配置

| 设备类型/厂商 | 推荐字节序 | 说明 |
|--------------|-----------|------|
| **标准 Modbus** | `EndianBig` | 符合 Modbus 协议规范 |
| **西门子 S7** | `EndianLittleBig` | S7-200/300/400 系列 |
| **施耐德/Modicon** | `EndianBig` 或 `EndianBigLittle` | 取决于配置 |
| **三菱 FX** | `EndianBigLittle` | FX 系列 PLC |
| **欧姆龙 CP1H** | `EndianBig` | 标准配置 |
| **ABB ACS880** | `EndianBig` | 变频器 |
| **禾望 PV** | `EndianBig` | 光伏逆变器 |
| **x86 PC** | `EndianLittle` | 内存直接存储 |

---

### Data 模板类详解

#### 类定义

```cpp
template <typename T, Endian e = EndianBig>
class Data;
```

- **T**: 要存储/传输的数据类型（必须是算术类型，且大小为2的倍数）
- **e**: 字节序和字序配置，默认为 `EndianBig`

#### 常用构造方式

```cpp
// 默认构造，值为0
Data<int32_t> value1;

// 从给定值构造
Data<int32_t> value2(12345);
Data<float> temp(25.5f);
Data<double> voltage(220.75);

// 复制构造
Data<int32_t> value3(value2);
```

#### 核心方法

```cpp
Data<int32_t> data(1000);

// 获取/设置数据值
data.value();           // 获取值
data = 2000;            // 设置值

// 获取寄存器数组引用
data.registers();       // 返回 std::array<uint16_t, N>&
data.registers().data(); // 返回 uint16_t* 指针

// 获取/设置字节序
data.endianness();              // 获取当前字节序
data.setEndianness(EndianLittle); // 设置字节序

// 其他方法
data.size();            // 返回类型T的字节数
data.print();           // 打印十六进制格式
```

---

### 各数据类型完整示例

#### 1. int16_t / short（16位有符号整数）

单个寄存器即可存储，无需关心字节序。

```cpp
// 读取
uint16_t raw[1];
slave.readRegisters(0, raw, 1);
int16_t value = static_cast<int16_t>(raw[0]);

// 使用Data模板类
Data<int16_t> temp;
slave.readRegisters(0, temp.registers().data(), 1);
cout << "温度: " << temp.value() << "°C" << endl;

// 写入
Data<int16_t> setpoint(-100);
slave.writeRegisters(10, setpoint.registers().data(), 1);

// 典型应用：带符号的温度值（-55°C ~ +125°C）
Data<int16_t> dht11_temp(-25);  // DS18B20温度传感器
slave.writeRegister(0, dht11_temp);
```

#### 2. uint16_t / unsigned short（16位无符号整数）

单个寄存器，直接读写即可。

```cpp
// 读取模拟量（0-65535 对应 0-10V）
uint16_t rawADC;
slave.readRegister(0, &rawADC);
float voltage = rawADC * 10.0f / 65535.0f;

// 使用Data模板类
Data<uint16_t> counter;
slave.readRegisters(0, counter.registers().data(), 1);
cout << "计数器: " << counter.value() << endl;

// 写入
Data<uint16_t> setpoint(5000);
slave.writeRegisters(10, setpoint.registers().data(), 1);
```

#### 3. int32_t / int（32位有符号整数）

占用2个寄存器，需要配置字节序。

```cpp
// ============================================
// 假设从机使用标准 Modbus 大端序 (ABCD)
// ============================================
Data<int32_t, EndianBig> power(1234567);
slave.writeRegisters(0, power.registers().data(), 2);

// 读取
Data<int32_t, EndianBig> energy;
slave.readRegisters(100, energy.registers().data(), 2);
cout << "累计电量: " << energy.value() << " kWh" << endl;

// ============================================
// 假设从机使用小端序 (DCBA) - x86架构
// ============================================
Data<int32_t, EndianLittle> encoder;
slave.readRegisters(200, encoder.registers().data(), 2);
cout << "编码器位置: " << encoder.value() << endl;

// ============================================
// 假设从机使用 CDAB 格式 - 部分 PLC
// ============================================
Data<int32_t, EndianBigLittle> flow_rate;
slave.readRegisters(300, flow_rate.registers().data(), 2);
cout << "流量: " << flow_rate.value() << " m³/h" << endl;

// 手动字节序转换示例
uint16_t raw[2];
slave.readRegisters(0, raw, 2);

// 大端序转换为本地值
int32_t value_big = (static_cast<int32_t>(raw[0]) << 16) | raw[1];

// 小端序转换为本地值
int32_t value_little = (static_cast<int32_t>(raw[1]) << 16) | raw[0];
```

#### 4. uint32_t / unsigned int（32位无符号整数）

与 int32_t 类似，但取值范围更大。

```cpp
// 读取设备序列号
Data<uint32_t, EndianBig> serial;
slave.readRegisters(0, serial.registers().data(), 2);
cout << "序列号: " << serial.value() << endl;

// 写入时间戳（Unix时间秒）
Data<uint32_t, EndianBig> timestamp(static_cast<uint32_t>(time(nullptr)));
slave.writeRegisters(10, timestamp.registers().data(), 2);

// 读取IP地址（每个字节对应一个寄存器）
Data<uint32_t, EndianBig> ip;
slave.readRegisters(20, ip.registers().data(), 2);
// 假设 raw = [192, 168] [1, 100]
// ip.value() = 0xC0A80164 = 3232235876
// 解析: 192<<24 | 168<<16 | 1<<8 | 100 = "192.168.1.100"
```

#### 5. int64_t / long long（64位有符号整数）

占用4个寄存器，常用于高精度计数器或时间戳（毫秒）。

```cpp
// 写入64位计数器
Data<int64_t, EndianBig> big_counter(1234567890123456LL);
slave.writeRegisters(0, big_counter.registers().data(), 4);

// 读取64位时间戳（毫秒）
Data<int64_t, EndianBig> timestamp_ms;
slave.readRegisters(100, timestamp_ms.registers().data(), 4);
cout << "时间戳: " << timestamp_ms.value() << " ms" << endl;

// 时间戳转换为标准格式
int64_t ms = timestamp_ms.value();
time_t seconds = ms / 1000;
long milliseconds = ms % 1000;
struct tm *tm_info = localtime(&seconds);
char buffer[64];
strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
printf("时间: %s.%03ld\n", buffer, milliseconds);
```

#### 6. uint64_t / unsigned long long（64位无符号整数）

常用于大容量的计数或能量统计。

```cpp
// 读取累计能耗（高分辨率）
Data<uint64_t, EndianBig> total_energy;
slave.readRegisters(0, total_energy.registers().data(), 4);
cout << "总能耗: " << total_energy.value() << " Wh" << endl;

// 写入
Data<uint64_t, EndianBig> set_value(0xFFFFFFFFFFFFFFFFULL);
slave.writeRegisters(200, set_value.registers().data(), 4);

// 注意事项：64位数据需要8字节，可能超出浮点数精度
// 建议使用整数运算，避免浮点数转换误差
```

#### 7. float（32位浮点数）

占用2个寄存器，是最常用的数据类型之一。

```cpp
// ============================================
// 读取浮点数据 - 标准大端序 (ABCD)
// ============================================
Data<float, EndianBig> temperature;
slave.readRegisters(0, temperature.registers().data(), 2);
cout << "温度: " << temperature.value() << " °C" << endl;

// ============================================
// 常见工业传感器配置
// ============================================

// 西门子 PLC (CDAB)
Data<float, EndianBigLittle> siemens_temp;
slave.readRegisters(0, siemens_temp.registers().data(), 2);

// 三菱 PLC (CDAB)
Data<float, EndianBigLittle> mitsubishi_current;
slave.readRegisters(10, mitsubishi_current.registers().data(), 2);

// 欧姆龙 (ABCD)
Data<float, EndianBig> omron_voltage;
slave.readRegisters(20, omron_voltage.registers().data(), 2);

// ============================================
// 写入浮点设置值
// ============================================
Data<float, EndianBig> set_temp(25.5f);
slave.writeRegisters(50, set_temp.registers().data(), 2);

Data<float, EndianBig> set_pressure(1.013f);  // 大气压
slave.writeRegisters(52, set_pressure.registers().data(), 2);

// ============================================
// 浮点数注意事项
// ============================================

// 1. 浮点数精度有限（7位有效数字）
//    1234567.0 和 1234568.0 可能表示为相同值

// 2. 浮点数不能准确表示所有十进制数
float a = 0.1f;
float b = 0.2f;
float c = a + b;
cout << "0.1 + 0.2 = " << c << endl;  // 可能输出 0.300000012

// 3. 某些特殊值会被限幅
Data<float> nan_value(std::nan(""));      // NaN
Data<float> inf_value(std::numeric_limits<float>::infinity());  // 无穷大
```

#### 8. double（64位浮点数）

占用4个寄存器，提供更高的精度（15位有效数字）。

```cpp
// 读取高精度数据
Data<double, EndianBig> high_precision;
slave.readRegisters(0, high_precision.registers().data(), 4);
cout << "高精度值: " << high_precision.value() << endl;

// 写入
Data<double, EndianBig> set_value(3.14159265358979);
slave.writeRegisters(100, set_value.registers().data(), 4);

// ============================================
// 64位浮点存储格式（大端序 ABCD ABCD）
// ============================================
// 寄存器0: 高16位
// 寄存器1: 中16位  
// 寄存器2: 中16位
// 寄存器3: 低16位

Data<double, EndianBig> pi(3.141592653589793);
// 内存布局: [40][09][21][FB][54][3F][EE][D6]
```

---

### 综合示例：读取多种数据类型

```cpp
#include <iostream>
#include <iomanip>
#include <modbuspp.h>

using namespace Modbus;

struct SensorData {
    Data<int16_t, EndianBig> temperature;    // 寄存器0    -55~125°C
    Data<int16_t, EndianBig> humidity;       // 寄存器1    0~100%
    Data<int32_t, EndianBig> pressure;       // 寄存器2-3  Pa
    Data<uint32_t, EndianBig> timestamp;     // 寄存器4-5  秒
    Data<float, EndianBig> wind_speed;       // 寄存器6-7  m/s
    Data<double, EndianBig> latitude;        // 寄存器8-11 度
    Data<double, EndianBig> longitude;       // 寄存器12-15度
};

int main() {
    Master mb(Tcp, "192.168.1.100", "502");
    Slave &slave = mb.addSlave(1);
    
    if (mb.open()) {
        SensorData data;
        
        // 批量读取所有数据（16个寄存器）
        uint16_t raw[16];
        if (slave.readRegisters(0, raw, 16) == 16) {
            // 解析各字段
            data.temperature.registers()[0] = raw[0];
            data.humidity.registers()[0] = raw[1];
            data.pressure.registers()[0] = raw[2];
            data.pressure.registers()[1] = raw[3];
            data.timestamp.registers()[0] = raw[4];
            data.timestamp.registers()[1] = raw[5];
            data.wind_speed.registers()[0] = raw[6];
            data.wind_speed.registers()[1] = raw[7];
            data.latitude.registers()[0] = raw[8];
            data.latitude.registers()[1] = raw[9];
            data.latitude.registers()[2] = raw[10];
            data.latitude.registers()[3] = raw[11];
            data.longitude.registers()[0] = raw[12];
            data.longitude.registers()[1] = raw[13];
            data.longitude.registers()[2] = raw[14];
            data.longitude.registers()[3] = raw[15];
            
            // 打印数据
            cout << fixed << setprecision(2);
            cout << "=== 气象站数据 ===" << endl;
            cout << "温度:    " << data.temperature.value() << " °C" << endl;
            cout << "湿度:    " << data.humidity.value() << " %" << endl;
            cout << "气压:    " << data.pressure.value() << " Pa" << endl;
            cout << "风速:    " << data.wind_speed.value() << " m/s" << endl;
            cout << "位置:    " << data.latitude.value() << ", " 
                 << data.longitude.value() << endl;
            cout << "时间戳:  " << data.timestamp.value() << " s" << endl;
        }
        
        mb.close();
    }
    
    return 0;
}
```

---

### 手动字节序转换

当不能使用 Data 模板类时，可以手动进行字节序转换。

#### 16位值（无需转换）

```cpp
// uint16_t 和 int16_t 直接存储，无需转换
uint16_t value = raw[0];
```

#### 32位值转换

```cpp
// 大端序 (Big Endian) → 本地值
uint32_t fromBigEndian(uint16_t *raw) {
    return (static_cast<uint32_t>(raw[0]) << 16) | raw[1];
}

// 小端序 (Little Endian) → 本地值
uint32_t fromLittleEndian(uint16_t *raw) {
    return (static_cast<uint32_t>(raw[1]) << 16) | raw[0];
}

// 本地值 → 大端序
void toBigEndian(uint32_t value, uint16_t *raw) {
    raw[0] = static_cast<uint16_t>(value >> 16);
    raw[1] = static_cast<uint16_t>(value & 0xFFFF);
}

// 本地值 → 小端序
void toLittleEndian(uint32_t value, uint16_t *raw) {
    raw[0] = static_cast<uint16_t>(value & 0xFFFF);
    raw[1] = static_cast<uint16_t>(value >> 16);
}
```

#### 浮点数转换

```cpp
// 大端序浮点数 → 本地浮点数
float fromBigEndianFloat(uint16_t *raw) {
    uint32_t bits = fromBigEndian(raw);
    float value;
    std::memcpy(&value, &bits, sizeof(float));
    return value;
}

// 本地浮点数 → 大端序
void toBigEndianFloat(float value, uint16_t *raw) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(float));
    toBigEndian(bits, raw);
}

// CDAB 格式浮点数（大端字节序，小端字序）
float fromCDABFloat(uint16_t *raw) {
    uint32_t bits = (static_cast<uint32_t>(raw[1]) << 16) | raw[0];
    float value;
    std::memcpy(&value, &bits, sizeof(float));
    return value;
}
```

#### 64位值转换

```cpp
// 大端序64位 → 本地值
uint64_t fromBigEndian64(uint16_t *raw) {
    return (static_cast<uint64_t>(raw[0]) << 48) |
           (static_cast<uint64_t>(raw[1]) << 32) |
           (static_cast<uint64_t>(raw[2]) << 16) |
           raw[3];
}

// 小端序64位 → 本地值
uint64_t fromLittleEndian64(uint16_t *raw) {
    return (static_cast<uint64_t>(raw[3]) << 48) |
           (static_cast<uint64_t>(raw[2]) << 32) |
           (static_cast<uint64_t>(raw[1]) << 16) |
           raw[0];
}

// 大端序64位浮点数 → 本地浮点数
double fromBigEndianDouble(uint16_t *raw) {
    uint64_t bits = fromBigEndian64(raw);
    double value;
    std::memcpy(&value, &bits, sizeof(double));
    return value;
}
```

---

### 字节序检测与自动适配

```cpp
#include <cstring>

class EndianHelper {
public:
    static bool isLittleEndian() {
        uint16_t test = 0x0001;
        uint8_t *p = reinterpret_cast<uint8_t*>(&test);
        return p[0] == 0x01;
    }
    
    static bool isBigEndian() {
        return !isLittleEndian();
    }
    
    // 检测远程设备的字节序（通过已知的测试值）
    static Modbus::Endian detectEndian(const uint16_t *raw, float knownValue) {
        // 假设大端序
        uint32_t bigBits = (static_cast<uint32_t>(raw[0]) << 16) | raw[1];
        float bigValue;
        std::memcpy(&bigValue, &bigBits, sizeof(float));
        
        // 假设小端序
        uint32_t littleBits = (static_cast<uint32_t>(raw[1]) << 16) | raw[0];
        float littleValue;
        std::memcpy(&littleValue, &littleBits, sizeof(float));
        
        // 比较哪个更接近已知值
        if (std::abs(bigValue - knownValue) < std::abs(littleValue - knownValue)) {
            return Modbus::EndianBig;
        }
        return Modbus::EndianLittle;
    }
};
```

---

### 常见问题与解决方案

#### Q1: 读取的浮点数完全错误

**问题**：读取的 float 值如 `1.07374e-08` 或 `-nan`

**原因**：字节序配置错误

**解决**：
```cpp
// 尝试不同的字节序配置
Data<float, EndianBig> v1;
Data<float, EndianBigLittle> v2;
Data<float, EndianLittleBig> v3;
Data<float, EndianLittle> v4;

// 逐个测试，找到正确的配置
slave.readRegisters(0, v1.registers().data(), 2);
if (isValidFloat(v1.value())) {
    // 使用 v1
}
```

#### Q2: 32位整数高16位正确，低16位全是0

**问题**：只读取了1个寄存器

**解决**：
```cpp
// 错误：只读1个寄存器
Data<int32_t> wrong(0);
slave.readRegisters(0, wrong.registers().data(), 1);  // 错误！

// 正确：读取2个寄存器
Data<int32_t> correct(0);
slave.readRegisters(0, correct.registers().data(), 2);  // 正确！
```

#### Q3: 负数显示为很大的正数

**问题**：将 `int16_t` 读取为 `uint16_t`

**解决**：
```cpp
uint16_t raw;
slave.readRegister(0, &raw);

// 错误
uint16_t temp = raw;

// 正确
int16_t temp = static_cast<int16_t>(raw);
```

#### Q4: 64位浮点数精度丢失

**问题**：64位浮点需要4个寄存器，但只读取了2个

**解决**：
```cpp
// 错误
Data<double, EndianBig> wrong;
slave.readRegisters(0, wrong.registers().data(), 2);  // 错误！

// 正确
Data<double, EndianBig> correct;
slave.readRegisters(0, correct.registers().data(), 4);  // 正确！
```

#### Q5: 部分设备要求特定的数据格式

**场景**：某些设备使用自定义的浮点格式

**示例**：IEEE 754 的变体或压缩格式

```cpp
// 假设设备使用 "3ABC" 格式：每寄存器存储3位十进制数
// 寄存器0=ABC0, 寄存器1=EFGH (E为符号位)
// 这需要自定义解析

float decodeCustomFloat(uint16_t *raw) {
    int16_t mantissa = (raw[0] >> 4) * 1000 + (raw[0] & 0xF) * 100 +
                       (raw[1] >> 12) * 10 + ((raw[1] >> 8) & 0xF);
    int8_t exponent = (raw[1] >> 4) & 0x0F;
    if (raw[1] & 0x8000) mantissa = -mantissa;  // 符号位
    return mantissa * std::pow(10.0f, exponent - 2);
}
```

---

## 通信方式详解

### RTU串口通信

#### 串口参数格式

```
BBBBP[S]
```
- `BBBB`: 波特率（1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200）
- `P`: 校验位（N=None, E=Even偶校验, O=Odd奇校验）
- `S`: 停止位（1或2，通常为1）

#### 常用配置示例

| 配置字符串 | 含义 |
|-----------|------|
| `"9600N1"` | 9600波特率，无校验，8位数据，1位停止 |
| `"19200E1"` | 19200波特率，偶校验，8位数据，1位停止 |
| `"115200O1"` | 115200波特率，奇校验，8位数据，1位停止 |

#### RTS控制（RS485）

对于RS485通信，可能需要控制RTS信号来切换发送/接收：

```cpp
Master mb(Rtu, "/dev/ttyUSB0", "9600E1");

// 配置RTS模式
mb.rtu().setRts(RtsDown);      // RTS低电平有效
mb.rtu().setRts(RtsUp);        // RTS高电平有效

// 设置串口模式
mb.rtu().setSerialMode(Rs485);  // RS485模式
mb.rtu().setSerialMode(Rs232); // RS232模式

// 设置RTS延时（毫秒）
mb.rtu().setRtsDelay(1000);
```

### TCP网口通信

#### 连接参数

```cpp
// TCP连接参数
// connection: IP地址或主机名
// settings:   端口号（字符串）

Master mb(Tcp, "192.168.1.100", "502");  // 连接到指定IP
Master mb(Tcp, "localhost", "1502");      // 连接到本机
Server srv(Tcp, "0.0.0.0", "502");        // 监听所有接口
Server srv(Tcp, "127.0.0.1", "1502");     // 仅监听本地
```

#### TCP服务端多客户端

```cpp
Server srv(Tcp, "0.0.0.0", "1502");
BufferedSlave &slave = srv.addSlave(1);
slave.setBlock(HoldingRegister, 100);

if (srv.open()) {
    srv.run();
    
    // 获取当前连接的客户端数
    cout << "当前连接数: " << srv.slave().rtu().socket() << endl;
}
```

---

## JSON配置文件

### 服务端JSON配置

```json
{
  "modbuspp-server": {
    "mode": "tcp",
    "connection": "0.0.0.0",
    "settings": "1502",
    "recovery-link": true,
    "debug": true,
    "response-timeout": 500,
    "byte-timeout": 500,
    "slaves": [
      {
        "id": 1,
        "blocks": [
          {
            "table": "holding-register",
            "starting-address": 0,
            "quantity": 10
          },
          {
            "table": "input-register",
            "starting-address": 0,
            "quantity": 5
          },
          {
            "table": "coil",
            "starting-address": 0,
            "quantity": 16
          }
        ]
      },
      {
        "id": 2,
        "blocks": [
          {
            "table": "holding-register",
            "quantity": 20
          }
        ]
      }
    ]
  }
}
```

### 客户端JSON配置

```json
{
  "modbuspp-master": {
    "mode": "tcp",
    "connection": "192.168.1.100",
    "settings": "502",
    "debug": true,
    "response-timeout": 1000,
    "slaves": [
      {
        "id": 1,
        "pdu-adressing": false
      },
      {
        "id": 2,
        "pdu-adressing": false
      }
    ]
  }
}
```

### 使用JSON初始化

```cpp
#include <iostream>
#include <modbuspp.h>

using namespace Modbus;

int main() {
    // 从JSON文件加载配置
    Server srv("config.json", "modbuspp-server");
    
    if (srv.open()) {
        srv.run();
        
        // 访问从机
        srv[1]->writeRegister(0, 100);
        srv[2]->writeRegister(0, 200);
        
        while (srv.isRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    return 0;
}
```

---

## 最佳实践与常见问题

### 最佳实践

#### 1. 连接管理

```cpp
class ModbusClient {
private:
    Master m_master;
    std::map<int, std::reference_wrapper<Slave>> m_slaves;
    
public:
    bool connect(const std::string& port, int baud, int slaveId) {
        m_master.setBackend(Rtu, port, std::to_string(baud) + "E1");
        m_master.setResponseTimeout(1000);
        
        if (m_master.open()) {
            m_slaves[slaveId] = std::ref(m_master.addSlave(slaveId));
            return true;
        }
        return false;
    }
    
    void disconnect() {
        if (m_master.isOpen()) {
            m_master.close();
        }
    }
    
    bool reconnect() {
        disconnect();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return m_master.open();
    }
};
```

#### 2. 错误处理

```cpp
#include <cerrno>
#include <cstring>

bool safeReadRegister(Slave& slave, int addr, uint16_t& value) {
    int ret = slave.readRegister(addr, &value);
    
    if (ret < 0) {
        std::cerr << "读取失败: " << std::strerror(errno) << std::endl;
        
        switch (errno) {
            case EMBADDATA:
                std::cerr << "数据地址非法" << std::endl;
                break;
            case EMBUSY:
                std::cerr << "从机忙，重试中..." << std::endl;
                break;
            case ETIMEDOUT:
                std::cerr << "通信超时" << std::endl;
                break;
            default:
                std::cerr << "未知错误" << std::endl;
                break;
        }
        return false;
    }
    return true;
}
```

#### 3. 数据采集循环

```cpp
#include <atomic>
#include <csignal>

std::atomic<bool> g_running(true);

void signalHandler(int) {
    g_running = false;
}

int main() {
    signal(SIGINT, signalHandler);
    
    Master mb(Tcp, "192.168.1.100", "502");
    Slave &slave = mb.addSlave(1);
    
    if (!mb.open()) {
        std::cerr << "连接失败" << std::endl;
        return 1;
    }
    
    while (g_running) {
        auto start = std::chrono::steady_clock::now();
        
        uint16_t data[10];
        if (slave.readInputRegisters(0, data, 10) == 10) {
            std::cout << "数据采集成功" << std::endl;
            // 处理数据...
        }
        
        // 固定间隔采集
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed < std::chrono::milliseconds(100)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100) - elapsed);
        }
    }
    
    mb.close();
    return 0;
}
```

### 常见问题

#### Q1: 编译时找不到 modbuspp.h

```bash
# 安装 libmodbuspp 开发包
sudo apt install libmodbuspp-dev libmodbuspp-doc

# 或使用 pkg-config
g++ main.cpp $(pkg-config --cflags --libs modbuspp)
```

#### Q2: 通信超时

```cpp
// 增加超时时间
mb.setResponseTimeout(3000);  // 3秒
mb.setByteTimeout(1000);      // 1秒

// 检查线路连接
mb.setDebug(true);  // 开启调试输出
```

#### Q3: RTU 地址解析错误

```cpp
// TCP模式下使用从机地址
Slave &slave = mb.addSlave(255);  // 默认值

// RTU模式下从机地址必须正确
Slave &slave = mb.addSlave(1);    // 从设备地址1-247
```

#### Q4: 数据字节序不匹配

```cpp
// 如果接收到的数据是 DCBA 而非 ABCD，使用小端序
Data<float, EndianLittle> value;
slave.readRegisters(0, value.registers().data(), 2);

// 或者手动调整字节序
uint16_t raw[2];
slave.readRegisters(0, raw, 2);
float value = (float)raw[0] * 256.0f + (float)raw[1];
```

#### Q5: 多线程安全

```cpp
// libmodbuspp 不是线程安全的，每个线程应使用独立的 Master/Server 实例
void threadFunc(int threadId) {
    Master mb(Tcp, "192.168.1.100", "502");
    Slave &slave = mb.addSlave(1);
    
    if (mb.open()) {
        while (true) {
            uint16_t value;
            slave.readInputRegisters(threadId, &value, 1);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

int main() {
    std::thread t1(threadFunc, 1);
    std::thread t2(threadFunc, 2);
    
    t1.join();
    t2.join();
}
```

### 错误码参考

| 错误码 | 说明 |
|--------|------|
| `EMBADDATA` | 非法数据 |
| `EBADCRC` | CRC校验失败 |
| `EBADEXC` | Modbus异常响应 |
| `EMBUNKEXC` | 未知异常 |
| `EMBDATA` | 数据访问错误 |
| `EMBBUSY` | 从机忙 |
| `ETIMEDOUT` | 通信超时 |
| `EINPROGRESS` | 连接进行中 |

---

## 附录

### CMakeLists.txt 集成

```cmake
cmake_minimum_required(VERSION 3.10)
project(modbus_app)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MODBUSPP REQUIRED libmodbuspp)

add_executable(my_app main.cpp)
target_include_directories(my_app PRIVATE ${MODBUSPP_INCLUDE_DIRS})
target_link_libraries(my_app PRIVATE ${MODBUSPP_LIBRARIES})
target_compile_options(my_app PRIVATE ${MODBUSPP_CFLAGS_OTHER})
```

### 头文件引用

```cpp
// 包含所有功能
#include <modbuspp.h>

// 或按需包含
#include <modbuspp/master.h>   // 客户端
#include <modbuspp/server.h>   // 服务端
#include <modbuspp/router.h>   // 路由器
#include <modbuspp/data.h>     // 数据模板类
```

### 完整示例项目结构

```
modbus_project/
├── CMakeLists.txt
├── config/
│   ├── server.json
│   └── client.json
├── include/
│   └── modbus_client.h
├── src/
│   ├── main.cpp
│   └── modbus_client.cpp
└── examples/
    ├── rtu_client.cpp
    ├── tcp_client.cpp
    └── tcp_server.cpp
```

---

*本文档基于 libmodbuspp 编写，适用于 Modbus RTU/TCP 通信开发。*

# iec104-python

**目录**

1. [简介](https://yuanbao.tencent.com/chat/naQivTmsDa/f788b304-5801-4c01-97d9-cc5703e835a2#简介)
2. [许可](https://yuanbao.tencent.com/chat/naQivTmsDa/f788b304-5801-4c01-97d9-cc5703e835a2#许可)
3. [系统要求](https://yuanbao.tencent.com/chat/naQivTmsDa/f788b304-5801-4c01-97d9-cc5703e835a2#系统要求)
4. [安装](https://yuanbao.tencent.com/chat/naQivTmsDa/f788b304-5801-4c01-97d9-cc5703e835a2#安装)
5. [文档](https://yuanbao.tencent.com/chat/naQivTmsDa/f788b304-5801-4c01-97d9-cc5703e835a2#文档)
6. [贡献](https://yuanbao.tencent.com/chat/naQivTmsDa/f788b304-5801-4c01-97d9-cc5703e835a2#贡献)

[English](README.md)| 中文

------

## 简介

本软件提供了一个面向对象的高级Python模块，用于模拟通过IEC 60870-5-104协议进行通信的SCADA系统和远程终端单元(RTU)。

Python模块c104结合了lib60870-C的使用与状态结构和Python回调处理程序。

**远程终端单元(RTU)示例**

```
import c104

# 服务器和站配置
server = c104.Server(ip="0.0.0.0", port=2404)

# 添加本地站和点
station = server极速赛车开奖结果记录_station(common_address=47)
measurement_point = station.add_point(io_address=11, type=c104.Type.M_ME_NC_1, report_ms=5000)
command_point = station.add_point(io_address=12, type=c104.Type.C_RC_TA_1)

server.start()
```

**SCADA主站单元示例**

```
import c104

client = c104.Client()

# 添加带站和点的RTU
connection = client.add_connection(ip="127.0.0.1", port=240极速赛车开奖结果记录, init=c104.Init.INTERROGATION)
station = connection.add_station(common_address=47)
measurement_point = station.add_point(io_address=11, type=c104.Type.M_ME_NC_1)
command_point = station.add_point(io_address=12, type=c104.Type.C_RC_TA_1)

client.start()
```

查看[examples](https://github.com/Fraunhofer-FIT-DIEN/iec104-python/tree/main/examples)文件夹获取更详细的示例。

## 许可

本软件根据GPLv3 (https://www.gnu.org/licenses/gpl-3.0.en.html)许可。

查看[LICENSE](https://github.com/Fraunhofer-FIT-DIEN/iec104-python/blob/main/LICENSE)文件获取完整的许可文本。

### 依赖项

#### lib60870-C

本项目基于MZ Automation GmbH的lib60870-C v2构建，该库根据[GPLv3](https://www.gnu.org/licenses/gpl-3.0.en.html)许可。

该库用于基于IEC 60870-5-104协议的通信。

[»源代码](https://github.com/mz-automation/lib60870)

[»文档](https://xn--support-ns2m343blyao17dlja577xiwvb4hfdoj29c.de/doc/lib60870/latest/index.html)

#### mbedtls

本项目基于Mbed TLS贡献者的mbedtls构建，该库根据[Apache-2.0](http://www.apache.org/licenses/LICENSE-2.0)许可。

该库用于为基于IEC 60870-5-104协议的通信添加传输层安全性。

[»源代码](https://github.com/Mbed-TLS/mbedtls)

[»文档](https://www.trustedfirmware.org/projects/mbed-tls/)

#### pybind11

本项目基于Wenzel Jakob的pybind11构建，该库根据[BSD风格许可](https://github.com/pybind/pybind11/blob/master/LICENSE)许可。

该库用于将C++代码包装成Python模块，并允许Python和C++之间的无缝互操作。

[»源代码](https://github.com/pybind/pybind11)

[»文档](https://pybind11.readthedocs.io/en/stable/)

#### catch2

本项目基于Catch2作者的catch2构建，该库根据[BSL-1.0](https://www.boost.org/users/license.html)许可。

该库用作测试自动化的测试框架。

[»源代码](https://github.com/catchorg/Catch2)

[»文档](https://github.com/catchorg/Catch2/blob/devel/docs/Readme.md)

## 系统要求

### 操作系统

- Manylinux (x86_64): 支持
- Manylinux (aarch64): 支持
- Raspbian (armv7l): 支持
- Windows (x64): 支持

### Python版本

- Python >= 3.7, < 3.13

## 安装

请根据您的需要调整版本号或使用特定版本。

### 从PyPI安装

```
python3 -m pip install c104
```

### 从Git安装（带标签）

```
python3 -m pip install c104@git+https://github.com/fraunhofer-fit-dien/iec104-python.git
```

您需要构建要求，列在"如何构建"下。

## 文档

在我们的[Read the Docs文档](https://iec104-python.readthedocs.io/latest/index.html)中阅读更多关于**类**及其**属性**的信息。

## 贡献

### 如何贡献

1. 使用GitHub的问题功能添加功能请求和报告错误
2. 创建拉取请求

### 如何构建（Linux）

1. 安装依赖项

   ```
   sudo apt-get install build-essential python3-pip python3-dev python3-dbg
   python3 -m pip install --upgrade pip
   ```

2. 克隆存储库

   ```
   git clone --depth=1 --branch=main https://github.com/Fraunhofer-FIT-DIEN/iec104-python.git
   cd iec104-python
   git submodule update --init
   ```

3. 构建wheel

   ```
   python3 -m pip wheel .
   ```

### 如何为多个Python版本构建（使用Docker的Linux）

1. 通过docker构建wheels（Linux）

   ```
   /bin/bash ./bin/linux-build.sh
   ```

### 如何分析性能（Linux）

1. 安装依赖项

   ```
   sudo apt-get install google-perftools valgrind
   sudo pip3 install yep
   ```

2. 复制pprof二进制文件

   ```
   cd /usr/bin
   sudo wget https://raw.githubusercontent.com/gperftools/gperftools/master/src/pprof
   sudo chmod +x pprof
   ```

3. 执行分析器脚本

   ```
   ./bin/profiler.sh
   ```

### 如何构建（Windows）

1. 安装依赖项

   - [Python 3](https://www.python.org/downloads/windows/)
   - [Visual Studio 2022构建工具](https://visualstudio.microsoft.com/de/downloads/)（向下滚动 »所有下载 »Visual Studio 2022工具）

2. 选项1：构建为wheel

   ```
   python3 -m pip wheel .
   ```

3. 选项2：通过PowerShell构建pyd

   ```
   cmake -DCMAKE_BUILD_TYPE=Release -G "Visual Studio 17 2022" -B cmake-build-release -A x64 -DPython_EXECUTABLE=C:\PATH_TO_PYTHON\python.exe
   &"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" /m /p:Platform=x64 /p:Configuration=Release c104.sln /t:Rebuild
   ```

   如果您有多个Python版本，请设置有效的PATH_TO_PYTHON。\

   除非msbuild已在PATH中，否则请设置到MSBuild.exe的有效路径。

### 生成文档

1. 构建c104模块

2. 安装依赖项

   - `python极速赛车开奖结果记录 -m pip install -r ./docs/requirements.txt`
   - doxygen
   - graphviz

3. 构建doxygen XML

   ```
   doxygen Doxyfile
   ```

4. 构建Sphinx HTML

   ```
   python3 bin/build-docs.py
   ```

## 变更日志

在我们的[变更日志](https://iec104-python.readthedocs.io/latest/changelog.html)文档中跟踪所有更改。
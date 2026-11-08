# 后端测试

测试按模块组织，由 `backend/tests/CMakeLists.txt` 注册到 CTest。

- `common/`：公共模型、编解码、配置校验和数据库接口。
- `rtdb/`：实时存储后端、四遥运行时测点、业务门面和 SDK 消费。
- `log/`：进程日志初始化、级别、轮转、通道日志及 Web 日志适配。
- `net/`：协议插件、处理器、采集连接复用和转发。
- `db/`：工程配置加载。
- `storage/`：历史采样、存储配置和存储流程。
- `web/`：Web 设备管理、身份认证及 HTTP 接口。
- `daemon/`：进程生命周期与共享资源清理。
- `util/`：公共工具。

旧 `test_models` 程序已不存在；无引用的设备、通道、分组、从站和公式映射模型已删除。
四遥运行时测点的序列化、更新和存取测试位于 `rtdb/point_import_test.cpp` 与
`rtdb/test_business.cpp`。

## 构建与运行

从仓库根目录执行。Windows MSVC 需使用已配置依赖的 x64 开发者终端：

```powershell
cmake --preset windows-msvc -S backend
cmake --build backend/build/windows-msvc --config Release --parallel 2
ctest --test-dir backend/build/windows-msvc -C Release --output-on-failure
```

仅运行日志和实时数据库测试：

```powershell
ctest --test-dir backend/build/windows-msvc -C Release --output-on-failure -R "process_logger_|web_framework_logging|realtime_backends|rtdb_dispatcher"
```

Linux 使用 `backend/CMakePresets.json` 中的 `linux-native` 配置及对应构建目录。
部分集成测试需要本机共享内存、回环连接或已配置的外部数据库；守护进程测试需要单实例锁可用。

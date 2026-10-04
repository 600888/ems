# 设备点表模板与协议示例

设备点表参考 `ems_simulate/data` 中的 XLSX 文件。本目录保留原始工作表、表头、示例数据和格式，供设备点表设计、填写和协议联调参考。

| 文件 | 用途 |
| --- | --- |
| [point_template.xlsx](point_template.xlsx) | 空白通用模板，包含遥测、遥信、遥控、遥调四个工作表 |
| [point_sample_modbus.xlsx](point_csv/point_sample_modbus.xlsx) | Modbus 点表示例，包含寄存器地址、功能码、解析码和系数 |
| [point_sample_iec104.xlsx](point_csv/point_sample_iec104.xlsx) | IEC 104 点表示例，包含信息体地址和 `104ASDU类型` |
| [point_sample_dlt645.xlsx](point_csv/point_sample_dlt645.xlsx) | DLT645 电表遥测示例，`寄存器地址` 列保存数据标识 DI |
| [point_sample_dnp3.xlsx](point_csv/point_sample_dnp3.xlsx) | DNP3 点表示例，包含点索引、变体、事件类别和控制参数 |
| [point_sample_opcua.xlsx](point_csv/point_sample_opcua.xlsx) | OPC UA 点表示例，包含 NodeId、命名空间 URI、数据类型和采样间隔 |

## 通用模板结构

首行为中文表头，数据从第二行开始。通用模板只有表头，按对应工作表填写点位；协议专用列参考相应示例。

| 工作表 | 字段 |
| --- | --- |
| 遥测 | 测点编码、测点名称、从机地址、寄存器地址、功能码、解析码、乘系数、加系数、上限值、下限值、104ASDU类型 |
| 遥信 | 测点编码、测点名称、从机地址、寄存器地址、功能码、解析码、位偏移、是否反转、104ASDU类型 |
| 遥控 | 测点编码、测点名称、从机地址、寄存器地址、功能码、解析码、位偏移、命令类型、关联遥信编码、104ASDU类型 |
| 遥调 | 测点编码、测点名称、从机地址、寄存器地址、功能码、解析码、乘系数、加系数、上限值、下限值、关联遥测编码、104ASDU类型 |

## 与 EMS 导入格式的关系

本目录的四遥 XLSX 使用 `ems::db::DbManager::import_point_table()` 导入，校验后以事务写入 `point_yc`、`point_yx`、`point_yk`、`point_yt` 配置表。支持 Modbus RTU/TCP、IEC 104 和 DLT645 遥测；DNP3、OPC UA 示例暂作为参考。旧版 `.xls` 需先另存为 `.xlsx`。调用方先选择已有实际设备与所属通道，协议从数据库通道读取。

公共模型的 `ems::config::import_point_table_file()` 另支持 [Modbus CSV v1](../docs/architecture/point-table-import.md) 和沿用其英文列名的 XLSX，生成内存草稿。该格式与本目录四遥中文模板分别对应公共模型和现有数据库配置接口。

四遥模板保存协议地址、解析类型与字节序、数值系数、限值、位偏移、反转标志、IEC 104 ASDU 类型和命令反馈关联。遥控关联遥信、遥调关联遥测；入库时在选定设备及通道内解析反馈 ID。追加模式拒绝已存在的编码，替换模式只替换选定设备及通道的配置。关联缺失、字段错误或数据库写入失败均阻止整批提交。

文件上限 2 MiB，整表上限 10000 个点，每张工作表最多 64 列；空行忽略，重复表头、重复编码、公式和无效地址会报工作表、行号及列名。

## 数据库接入

已有 MySQL 配置库先执行 [Excel 点表迁移](../backend/data/point_table_excel_migration.sql)，补齐 ASDU 类型、遥调反馈 ID 和编码唯一索引。迁移可重复运行；历史重复编码需先处理。新库由 `ensure_database()` 创建完整结构，设备和通道须提前建立。

```cpp
ems::db::DbManager database;
if (!database.connect(ems::db::get_default_db_config())) {
    // database.last_error() 是连接错误。
}
auto result = database.import_point_table(
    "data/point_csv/point_sample_modbus.xlsx", channel_id, device_id,
    ems::db::PointTableImportMode::Append);
// result.committed 为 true 才表示已入库；result.errors 包含失败位置与原因。
```

启用 `BUILD_EXAMPLES` 构建后可使用 `ems_point_import`，数据库连接使用 `EMS_DB_CONF` 或 `EMS_DB_*` 环境变量：

```powershell
# 参数顺序：文件、设备 ID、通道 ID、模式。
./backend/build/windows-mingw/bin/ems_point_import.exe ./data/point_csv/point_sample_modbus.xlsx 1 1 append
```

导入只保存配置；固有点绑定和运行配置发布按 [设备模型与点表绑定设计](../docs/architecture/device-model.md) 另行完成：

- 测点编码用于厂家点身份，EMS 固有点的 `standard_code` 需单独绑定。
- `从机地址` 属于实例通道绑定；复用型号模板时应与厂家点定义分离。
- 地址列按协议解释，并明确 Modbus 文档编号与零基偏移的区别。
- 解析码、字节序、系数、单位和位偏移需要显式转换；CSV v1 尚不支持位段及完整命令配置。
- 遥控/遥调及其关联反馈点分别建模，命令类型和反馈关联需按 EMS 命令配置校验。

示例中的地址、系数和关联点用于模拟联调，实际设备填写以厂家协议为准。

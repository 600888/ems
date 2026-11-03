# 点表 CSV/XLSX 导入与内存加载（v1）

本格式用于公共 C++ 模块中的 Modbus 厂家点表草稿。实现入口是 `parse_point_table_csv()`、`import_point_table_file()` 和 `load_point_table()`，示例见 [`point_table_v1_example.csv`](https://github.com/600888/ems/blob/main/backend/data/point_table_v1_example.csv)。导入前由调用方明确选定 `device_id` 与设备通道 `binding_id`；文件不含实际 IP、串口、从站地址或设备 ID。点表使用带列名的格式，按先选定设备、再导入点表的顺序处理。

设备点表的 Excel 格式包含遥测、遥信、遥控、遥调四个工作表，空白模板放在 [`data/point_template.xlsx`](https://github.com/600888/ems/blob/main/data/point_template.xlsx)，各协议示例和数据库导入说明见 [`data/README.md`](https://github.com/600888/ems/blob/main/data/README.md)。四遥中文模板通过 `ems::database::import_configuration_points()` 校验并以事务写入现有配置表，支持 Modbus RTU/TCP、IEC 104 和 DLT645 遥测。

本节的 `import_point_table_file()` 也支持 XLSX，但使用下面列出的英文列名，读取单工作表或名为“点表”的工作表，再复用 CSV 字段校验与内存加载逻辑。两种格式均拒绝公式和旧版 `.xls`；读取不修改源文件。

| 列 | 规则 |
| --- | --- |
| `template_key` | 设备内稳定厂家点键，非显示名称，文件内唯一 |
| `standard_code` | 对应设备类型的固有点 code；留空表示仅保留厂家原始行供审核 |
| `vendor_name` | 厂家点显示名称，可使用 CSV 引号包含逗号 |
| `address_space` | `coil`、`discrete_input`、`input_register`、`holding_register` |
| `address_offset` | 明确为 Modbus 协议零基偏移，十进制 0–65535；写入 `40001` 就是偏移 40001，不会自动减去文档编号基数 |
| `access` | `read` 或 `write`；输入寄存器和离散输入只能读 |
| `wire_type` | `bool`、`i16`、`u16`、`i32`、`u32`、`f32`；线圈和离散输入只用 `bool` |
| `byte_order` | 双寄存器点必须是 `ABCD`、`BADC`、`CDAB` 或 `DCBA`；其他点留空 |
| `scale`、`offset` | 厂家解码后的工程值映射为标准值：`standard = vendor × scale + offset`；有限数且 scale 非零，布尔点固定为 1 和 0 |
| `unit` | 映射后的标准单位，必须与对应固有点定义一致；无单位留空 |

文件使用 UTF-8，可带 BOM；首行列名，允许额外列和空行。上限 2 MiB、10000 行。解析返回精确的物理行号、列名和原因；任何错误都会阻止整表加载。导入只解析，不发送设备命令。

加载要求目录内已有选定设备、设备类型版本、固有点实例和 Modbus 通道绑定。`Append` 只加入尚无权威路由的标准点；`Replace` 在候选副本中重建该设备的标准投影路由。只有全部行和模型校验成功才返回新目录与草稿；失败时返回原目录和草稿。相同 `template_key` 在替换时形成稳定路由 ID，标准 `point_id` 不变。未绑定的厂家行保留在加载结果的 `vendor_rows`，等待后续持久化或审核。

```cpp
auto imported = ems::config::import_point_table_file(path, device_id, binding_id);
if (!imported.ok()) return imported.issues;

auto loaded = ems::config::load_point_table(
    catalog, draft, imported, ems::config::PointTableLoadMode::Replace);
if (!loaded.ok()) return loaded.issues;
// loaded.catalog 与 loaded.draft 是新的内存候选配置。
```

本节公共模型接口生成**内存配置草稿**。`write` 行可生成待审核的可写路由，但固定写入值、数值编码、反馈、互斥资源和执行授权仍须由命令配置补齐后才能投运。四遥 XLSX 的数据库入库接口不自动发布该公共模型草稿；标准点绑定的版本化持久化、导入任务幂等、文件 SHA-256、BMS 子节点展开仍未接入。

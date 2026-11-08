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

本目录的四遥 XLSX 由 Web 单设备导入和整站 ZIP 导入共用解析器处理，分别保存到 `cfg_point_yc`、`cfg_point_yx`、`cfg_point_yk`、`cfg_point_yt`。支持 Modbus RTU/TCP、IEC 104 和 DLT645 遥测；DNP3、OPC UA 示例暂作为参考。旧版 `.xls` 需先另存为 `.xlsx`。单设备导入先选择设备及绑定，协议从绑定的通道读取；整站包从 manifest 中读取协议与绑定。

公共模型的 `ems::config::import_point_table_file()` 另支持 [Modbus CSV v1](../website/docs/architecture/point-table-import.md) 和沿用其英文列名的 XLSX，生成内存草稿。该格式与本目录四遥中文模板分别对应公共模型和现有数据库配置接口。

四遥模板保存协议地址、解析类型与字节序、数值系数、限值、位偏移、反转标志、IEC 104 ASDU 类型。可选列 `单位` 保存工程单位。单设备 source_kind 可填 `telemetry_xlsx`，或使用 `vendor_xlsx` 自动识别四遥工作表；整站包自动识别。原英文 `点表` 格式仍支持 Modbus。

IEC 104 的 `从机地址` 表示公共地址，`寄存器地址` 表示 24 位信息体地址，`104ASDU类型` 必须与所在四遥类别匹配。地址写入 `protocol_address_json` 的 `casdu`、`ioa`、`iec104_type`，不会伪装成 Modbus 寄存器。Modbus 地址原样作为零基偏移使用，保留功能码、解析码、字节序及位偏移。DLT645 的地址列保存 DI，实际 12 位表号取设备绑定。

遥控关联遥信、遥调关联遥测，解析阶段检查关联编码存在且类别匹配。源关联编码和命令类型保留于导入记录及协议扩展字段；它们不能推断命令期望值、容差或控制授权，写点落库后仍停用，需补齐执行与反馈规则。保存即修改当前配置，站根 `config_revision` 递增，消费者据此重建运行目录。

文件上限 2 MiB，整表上限 10000 个点，每张工作表最多 64 列；空行忽略，重复表头、重复编码、公式和无效地址会报工作表、行号及列名。

## 整站导入

ZIP 根目录包含 `manifest.json` 和所引用的 XLSX。设备层级、通道、通信地址在 manifest 中声明；Excel 只保存点定义。格式见 [`backend/data/station_package_example`](../backend/data/station_package_example)。

前端先上传到 `/api/station-imports/inspect`，只读检查包内站编码和当前站编码。两者不一致时弹框提示，用户选择“继续导入”后再调用 `create`，取消不会创建任何设备、通道或任务。后端不按站编码一致性拦截导入，始终以当前站根作为归属，并在返回值中保留包内站编码和不一致警告。

`point_files[]` 可选 `target_device_code` 指定点归属部件，`device_code` 仍指实际通信设备。部件必须通过 `source_device_code` 指回该设备。多绑定设备必须填写 `binding_code`；不同簇可以复用同一模板文件，从机地址列留空时使用各自绑定地址。相同 XLSX 模板可用于多个簇，不需要复制为多个文件。

整站上传校验通过后，在一个事务内保存设备、通道、绑定及全部四遥点位，并递增一次站根配置计数，无需逐设备确认或应用。存在硬错误时整包不写入。重复上传相同 ZIP 不会重复生成点位，也能补齐此前只生成待应用任务的导入。IEC61850 使用 ICD/CID 配置文件接入，不使用四遥 Excel；整站点表入口不会自动消费随包附带的 ICD/CID。

## 数据库接入

关系数据库使用 [SQLAlchemy ORM 与 Alembic](../backend/python/README.md) 初始化：
编辑 `backend/python/database.yaml`，在仓库根目录安装 `python -m pip install -e ./backend/python`，
再执行 `ems-db init` 和 `ems-db seed catalog`；需要开发样例时另行执行 `ems-db seed demo`。
MySQL / PostgreSQL 需先创建空数据库并安装对应驱动。应用不自动建库、改表或写示例。
TDengine 历史库仍需由部署单独准备，不在当前 SQLAlchemy 迁移范围内。

点表通过 `/api/point-imports/create` 生成预览，再执行 `validate` 与 `apply` 保存当前配置。数据库错误带写入确定性，提交 Unknown 时先核验数据库，再决定后续操作。固有点绑定按 [设备模型与点表绑定设计](../website/docs/architecture/device-model.md) 完成：

- 测点编码用于厂家点身份，EMS 固有点的 `standard_code` 需单独绑定。
- `从机地址` 属于实例通道绑定；复用型号模板时应与厂家点定义分离。
- 地址列按协议解释，并明确 Modbus 文档编号与零基偏移的区别。
- 解析码、字节序、系数、单位和位偏移需要显式转换；CSV v1 尚不支持位段及完整命令配置。
- 遥控/遥调及其关联反馈点分别建模，命令类型和反馈关联需按 EMS 命令配置校验。

示例中的地址、系数和关联点用于模拟联调，实际设备填写以厂家协议为准。

# 整站点表打包导入示例（路径 A）

一个可以直接打包上传的完整例子：`manifest.json`（层级关系 + 每设备通讯参数 + 文件映射）
加每设备一个点表 Excel。把本目录内容打包成 zip（`station001.zip` 是已打包好的成品），
通过 `POST /api/station-imports/create` 上传（multipart：`metadata` 字段传 `{}`，
`file` 字段传 zip）。

## 包内容

| 文件 | 说明 |
| --- | --- |
| `manifest.json` | 清单：设备树层级、通道/绑定（通讯参数）、点表文件映射 |
| `pcs01.xlsx` | PCS 点表，`点表` 工作表，semantic_code 对应 `pcs-v1` 固有点 |
| `meter01.xlsx` | 关口电表点表，standard_code 留空 = 厂家扩展点 |
| `station001.zip` | 以上三个文件打包好的上传成品 |

## manifest 要点

- `station.code` 必须与已初始化的站根一致（站根不可通过 API 创建）。
- `devices[]` 顺序即创建顺序：`unit01`（单元，挂站根）→ `pcs01/bms01`（设备，挂单元）→
  `rack01/rack02`（部件，挂 BMS 下并用 `source_device_code` 指向采集它的通信设备）→
  `meter01`（站级公共设备，`parent_code` 留空直接挂站根）。
- `channels[]` + `bindings[]` 是 Excel 携带不了的通讯参数；协议白名单
  modbus/iec104/dlt645，绑定目标必须是 `node_kind=device`。
- `point_files[]`：省略 `binding_code` 时取该设备唯一的采集绑定；
  `address_base` 为 `zero`（寄存器地址从 0 计）或 `one`（从 1 计）。

## 点表 Excel 列格式（与单设备导入完全一致）

`template_key, standard_code, vendor_name, address_space, address_offset, access,
wire_type, byte_order, scale, offset, unit`

- `address_space`：`coil / discrete_input / input_register / holding_register`
- `access`：`read / write`（write 即遥控或遥调）
- `wire_type`：`bool / i16 / u16 / i32 / u32 / f32`（bool 点的 scale/offset 必须为 1/0）
- `byte_order`：双寄存器点（i32/u32/f32）必须填 `ABCD/BADC/CDAB/DCBA` 之一；
  单寄存器与线圈点留空
- `standard_code` 填固有点语义码（如 `active_power`，见 `seeds/device_management_seed.sql`）；
  留空即厂家扩展点，导入后按 `template_key` 幂等更新

## 导入后的确认流程（不自动生效）

导入返回每个设备的 `import_job_id + preview_hash` 与行数统计，逐设备执行：

1. `POST /api/point-imports/rows` 查看逐行预览（错误行/重叠 warning）；
2. `POST /api/point-imports/update-row` 逐行修正或确认（`accepted/fixed/rejected`）；
3. `POST /api/point-imports/validate` 校验通过后 `apply`（带 `expected_preview_hash`）。

重复上传同一个包 = 精确重放（稳定 ID 由包内容派生），不会产生重复设备。

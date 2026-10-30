# C/C++ 代码约定

- 自有 C/C++ 代码使用仓库根目录的 `.clang-format`，以 Google 风格为基础，保留项目的 4 空格缩进和 100 列限制。
- 新增或修改头文件中的函数声明时，在声明前写 Doxygen 注释。说明接口用途，并为每个具名参数标明 `@param[in]`、`@param[out]` 或 `@param[in,out]` 及其含义；非 `void` 返回值写 `@return`。涉及错误或所有权语义时一并说明。
- 已有的接口说明统一使用 Doxygen 语法（`/** ... */` 或 `///`）。实现内部的解释性注释按需使用普通 `//`，避免逐行复述代码。
- 第三方代码不参与注释或格式整改。

# 开发阶段与配置模型

- 项目仍在开发阶段，**不要引入配置版本**：没有草稿/发布/快照/回执/回滚，配置表允许原地 `UPDATE`/`DELETE`，保存即生效。热加载靠站根 `cfg_device.config_revision` 单调递增，消费者轮询比对后重建运行目录。不要新增 `cfg_version`、`version_id` 或配置版本语义的 `*_version` 列与表。
- **不要引入文件代际编号**：不新增 `004_*.sql` 这类新一代迁移脚本，不创建 `*_v2.*` / `*_v2_mysql.sql` 文件，设计文档不写“版本：x.y”“前版：…”这类抬头。要改表或改设计，直接改既有文件：
  - `backend/data/migrations/003_device_management_mysql.sql`
  - `backend/data/seeds/device_management_seed.sql`
  - `backend/include/common/database/configuration/device_management.h`（行结构）
  - `backend/src/common/db/configuration/device_management_rows.h`（ormpp 映射）
  - `website/docs/common/database/device-management.md`
- 例外：`type_version`、`template_version`、`profile_version`、`firmware_version` 是业务数据（设备类型 / 型号模板 / 协议 Profile / 固件版本），按业务需要保留。
- 设备管理只有“设备”，没有“资产”：站、单元、设备、部件统一在 `cfg_device`，用 `node_kind` 区分，`component` 通过 `source_device_id` 指向采集它的通信设备。不要恢复 `cfg_asset` / `asset_id` / `asset_path`。
- 电力四遥拆成四张表 `cfg_point_yc` / `cfg_point_yx` / `cfg_point_yk` / `cfg_point_yt`，代码对应 `YcPoint` / `YxPoint` / `YkPoint` / `YtPoint` 四类（共享 `PointBase`）。不要再合并成单表加 `point_type` 列；`PointKind` 只表示实时记录种类，遥控与遥调都映射为 `Command`，运行侧靠 `point_type` 区分。

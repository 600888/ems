-- ===========================================================================
-- 设备管理基础目录种子：设备类型 + 固有点目录
--
-- 与 backend/src/common/model/device_models.cpp 保持一致：
--   PCS 28 个固有点（遥测 18 / 遥信 5 / 遥控 3 / 遥调 2）
--   BMS 14 个、消防 5 个
-- 幂等：重复执行只更新名称/能力/必需标记，不删除已有行。
-- 需要先执行 003_device_management_mysql.sql。
-- ===========================================================================

SET NAMES utf8mb4;

-- 新工程必须有且只有一个站根；已有站根不覆盖，不创建第二个根。
INSERT INTO cfg_device
    (device_id, parent_device_id, node_kind, code, name, path, depth, enabled)
SELECT 'station', '', 'station', 'station', '储能站', '/', 0, 1
WHERE NOT EXISTS (SELECT 1 FROM cfg_device WHERE node_kind = 'station');

-- 一、设备类型
INSERT INTO cfg_device_type (type_code, type_version, name, category, status, description) VALUES
    ('pcs', 'pcs-v1', '储能变流器', 'pcs', 'active', ''),
    ('bms', 'bms-v1', '电池管理系统', 'bms', 'active', ''),
    ('fire', 'fire-v1', '消防设备', 'fire', 'active', ''),
    ('hvac', 'hvac-v1', '温控设备', 'hvac', 'active', ''),
    ('env', 'env-v1', '环境监测', 'env', 'active', ''),
    ('meter', 'meter-v1', '电表', 'meter', 'active', ''),
    ('relay', 'relay-v1', '保护装置', 'relay', 'active', ''),
    ('battery', 'battery-v1', '电池系统', 'battery', 'active', ''),
    ('rack', 'rack-v1', '电池簇', 'rack', 'active', ''),
    ('pack', 'pack-v1', '电池包', 'pack', 'active', ''),
    ('cell', 'cell-v1', '电芯', 'cell', 'active', '')
ON DUPLICATE KEY UPDATE name = VALUES(name), category = VALUES(category),
    status = VALUES(status);

-- 二、固有点目录（point_type 决定映射行落在哪张点表）
INSERT INTO cfg_type_point
    (type_code, type_version, semantic_code, name, point_type, value_type, unit, required,
     capability_codes_json, writable, stale_after_ms, sort_order) VALUES
    ('pcs', 'pcs-v1', 'run_state', '运行状态', 'yx', 'string', '', 1, '["start_stop"]', 0, NULL, 1),
    ('pcs', 'pcs-v1', 'control_mode', '控制模式', 'yx', 'string', '', 1, '["start_stop", "set_active_power", "set_reactive_power"]', 0, NULL, 2),
    ('pcs', 'pcs-v1', 'fault_active', '故障总信号', 'yx', 'bool', '', 0, '[]', 0, NULL, 3),
    ('pcs', 'pcs-v1', 'fault_code', '故障码', 'yx', 'string', '', 0, '[]', 0, NULL, 4),
    ('pcs', 'pcs-v1', 'fault_event', '故障事件', 'yx', 'string', '', 0, '[]', 0, NULL, 5),
    ('pcs', 'pcs-v1', 'active_power', '有功功率', 'yc', 'double', 'kW', 1, '["monitor_power", "set_active_power"]', 0, NULL, 6),
    ('pcs', 'pcs-v1', 'reactive_power', '无功功率', 'yc', 'double', 'kvar', 1, '["set_reactive_power"]', 0, NULL, 7),
    ('pcs', 'pcs-v1', 'active_power_setpoint', '有功设定回读', 'yc', 'double', 'kW', 0, '[]', 0, NULL, 8),
    ('pcs', 'pcs-v1', 'reactive_power_setpoint', '无功设定回读', 'yc', 'double', 'kvar', 0, '[]', 0, NULL, 9),
    ('pcs', 'pcs-v1', 'dc_voltage', '直流电压', 'yc', 'double', 'V', 0, '[]', 0, NULL, 10),
    ('pcs', 'pcs-v1', 'dc_current', '直流电流', 'yc', 'double', 'A', 0, '[]', 0, NULL, 11),
    ('pcs', 'pcs-v1', 'ac_voltage_a', 'A 相电压', 'yc', 'double', 'V', 0, '[]', 0, NULL, 12),
    ('pcs', 'pcs-v1', 'ac_voltage_b', 'B 相电压', 'yc', 'double', 'V', 0, '[]', 0, NULL, 13),
    ('pcs', 'pcs-v1', 'ac_voltage_c', 'C 相电压', 'yc', 'double', 'V', 0, '[]', 0, NULL, 14),
    ('pcs', 'pcs-v1', 'ac_voltage_ab', 'AB 线电压', 'yc', 'double', 'V', 0, '[]', 0, NULL, 15),
    ('pcs', 'pcs-v1', 'ac_voltage_bc', 'BC 线电压', 'yc', 'double', 'V', 0, '[]', 0, NULL, 16),
    ('pcs', 'pcs-v1', 'ac_voltage_ca', 'CA 线电压', 'yc', 'double', 'V', 0, '[]', 0, NULL, 17),
    ('pcs', 'pcs-v1', 'ac_current_a', 'A 相电流', 'yc', 'double', 'A', 0, '[]', 0, NULL, 18),
    ('pcs', 'pcs-v1', 'ac_current_b', 'B 相电流', 'yc', 'double', 'A', 0, '[]', 0, NULL, 19),
    ('pcs', 'pcs-v1', 'ac_current_c', 'C 相电流', 'yc', 'double', 'A', 0, '[]', 0, NULL, 20),
    ('pcs', 'pcs-v1', 'frequency', '电网频率', 'yc', 'double', 'Hz', 0, '[]', 0, NULL, 21),
    ('pcs', 'pcs-v1', 'max_charge_power', '最大充电功率', 'yc', 'double', 'kW', 0, '[]', 0, NULL, 22),
    ('pcs', 'pcs-v1', 'max_discharge_power', '最大放电功率', 'yc', 'double', 'kW', 0, '[]', 0, NULL, 23),
    ('pcs', 'pcs-v1', 'start', '开机', 'yk', 'bool', '', 1, '["start_stop"]', 1, NULL, 24),
    ('pcs', 'pcs-v1', 'stop', '关机', 'yk', 'bool', '', 1, '["start_stop"]', 1, NULL, 25),
    ('pcs', 'pcs-v1', 'fault_reset', '故障复位', 'yk', 'bool', '', 0, '[]', 1, NULL, 26),
    ('pcs', 'pcs-v1', 'set_active_power', '有功设定', 'yt', 'double', 'kW', 1, '["set_active_power"]', 1, NULL, 27),
    ('pcs', 'pcs-v1', 'set_reactive_power', '无功设定', 'yt', 'double', 'kvar', 1, '["set_reactive_power"]', 1, NULL, 28),
    ('bms', 'bms-v1', 'soc', 'SOC', 'yc', 'double', '%', 1, '["read_soc", "monitor_battery"]', 0, NULL, 29),
    ('bms', 'bms-v1', 'soh', 'SOH', 'yc', 'double', '%', 0, '[]', 0, NULL, 30),
    ('bms', 'bms-v1', 'pack_voltage', '电池堆电压', 'yc', 'double', 'V', 1, '["monitor_battery"]', 0, NULL, 31),
    ('bms', 'bms-v1', 'pack_current', '电池堆电流', 'yc', 'double', 'A', 1, '["monitor_battery"]', 0, NULL, 32),
    ('bms', 'bms-v1', 'max_charge_current', '最大充电电流', 'yc', 'double', 'A', 0, '[]', 0, NULL, 33),
    ('bms', 'bms-v1', 'max_discharge_current', '最大放电电流', 'yc', 'double', 'A', 0, '[]', 0, NULL, 34),
    ('bms', 'bms-v1', 'cell_voltage_min', '单体最低电压', 'yc', 'double', 'V', 0, '[]', 0, NULL, 35),
    ('bms', 'bms-v1', 'cell_voltage_max', '单体最高电压', 'yc', 'double', 'V', 0, '[]', 0, NULL, 36),
    ('bms', 'bms-v1', 'cell_temperature_min', '单体最低温度', 'yc', 'double', '℃', 0, '[]', 0, NULL, 37),
    ('bms', 'bms-v1', 'cell_temperature_max', '单体最高温度', 'yc', 'double', '℃', 0, '[]', 0, NULL, 38),
    ('bms', 'bms-v1', 'charge_allowed', '允许充电', 'yx', 'bool', '', 0, '[]', 0, NULL, 39),
    ('bms', 'bms-v1', 'discharge_allowed', '允许放电', 'yx', 'bool', '', 0, '[]', 0, NULL, 40),
    ('bms', 'bms-v1', 'contactor_state', '接触器状态', 'yx', 'string', '', 0, '[]', 0, NULL, 41),
    ('bms', 'bms-v1', 'fault_active', '故障总信号', 'yx', 'bool', '', 1, '["monitor_battery"]', 0, NULL, 42),
    ('fire', 'fire-v1', 'fire_alarm', '火警', 'yx', 'bool', '', 1, '["monitor_fire"]', 0, NULL, 43),
    ('fire', 'fire-v1', 'fault_active', '故障总信号', 'yx', 'bool', '', 1, '["monitor_fire"]', 0, NULL, 44),
    ('fire', 'fire-v1', 'smoke_alarm', '烟雾报警', 'yx', 'bool', '', 0, '[]', 0, NULL, 45),
    ('fire', 'fire-v1', 'temperature_alarm', '温度报警', 'yx', 'bool', '', 0, '[]', 0, NULL, 46),
    ('fire', 'fire-v1', 'discharge_state', '气体喷洒状态', 'yx', 'string', '', 0, '[]', 0, NULL, 47)
ON DUPLICATE KEY UPDATE name = VALUES(name), point_type = VALUES(point_type),
    value_type = VALUES(value_type), unit = VALUES(unit), required = VALUES(required),
    capability_codes_json = VALUES(capability_codes_json), writable = VALUES(writable),
    sort_order = VALUES(sort_order);

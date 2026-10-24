-- ===========================================================================
-- 设备管理表：14 张
--
-- 设计依据：website/docs/common/database/device-management.md
--   · 只有设备，没有资产：站/单元/设备/部件统一在 cfg_device，node_kind 区分
--   · 电力四遥拆四张点表：cfg_point_yc / _yx / _yk / _yt
--   · 开发阶段直接改工程表：没有配置版本、没有草稿/发布/快照，
--     保存即生效，热加载靠站根节点的 config_revision 单调递增
--
-- 本脚本替代旧的 42 表设计（含 cfg_asset 资产树与 cfg_version 版本体系）。
-- 新库直接执行 000 + 003 + seeds/device_management_seed.sql；
-- 已有旧表的环境按文档第 9 节迁移后删除旧表。
--
-- 约定：
--   1. 表可原地 UPDATE/DELETE，不需要版本行；变更审计由统一审计模块负责。
--   2. 稳定业务 ID（device_id/point_id/channel_id/binding_id）是文本，
--      跨重复导入保持不变；数字自增主键仅供连接。
--   3. 语义为空时用空串而不是 NULL：ormpp 行映射只有算术类型会被清零，
--      唯一性用函数索引 NULLIF(col,'') 保留“空值不参与唯一”的语义。
--   4. 本脚本只建表，不写业务数据；设备类型与固有点目录见种子脚本。
--   5. MySQL 8.0.16+（CHECK 与函数索引生效）。约束名在库内唯一，按表加前缀。
-- ===========================================================================

SET NAMES utf8mb4;

-- ===========================================================================
-- 一、设备树与类型
-- ===========================================================================

CREATE TABLE IF NOT EXISTS cfg_device (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    device_id VARCHAR(64) NOT NULL COMMENT '稳定设备 ID，全树唯一',
    parent_device_id VARCHAR(64) NOT NULL DEFAULT '' COMMENT '父节点；站根为空串',
    node_kind ENUM('station', 'unit', 'device', 'component') NOT NULL,
    code VARCHAR(64) NOT NULL COMMENT '同父节点下唯一',
    name VARCHAR(128) NOT NULL DEFAULT '',
    path VARCHAR(512) NOT NULL COMMENT '相对站根的路径，如 /unit01/battery01/rack01',
    depth TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0-8',
    sort_order INT NOT NULL DEFAULT 0,
    device_type_code VARCHAR(32) NOT NULL DEFAULT '' COMMENT '部件节点同样引用类型',
    type_version VARCHAR(16) NOT NULL DEFAULT '',
    template_id VARCHAR(64) NOT NULL DEFAULT '' COMMENT '创建时套用的型号模板',
    source_device_id VARCHAR(64) NOT NULL DEFAULT '' COMMENT 'component 必填：采集它的通信设备',
    vendor VARCHAR(64) NOT NULL DEFAULT '',
    model VARCHAR(64) NOT NULL DEFAULT '',
    firmware_version VARCHAR(32) NOT NULL DEFAULT '',
    rated_power_kw DECIMAL(12, 3) NULL,
    timezone VARCHAR(64) NOT NULL DEFAULT 'Asia/Shanghai' COMMENT '仅站根使用',
    default_stale_after_ms INT UNSIGNED NOT NULL DEFAULT 60000
        COMMENT '仅站根使用；0 表示不判陈旧',
    config_revision BIGINT UNSIGNED NOT NULL DEFAULT 0
        COMMENT '仅站根使用：每次保存 +1，供热加载轮询',
    enabled TINYINT(1) NOT NULL DEFAULT 1 COMMENT '配置启用，与运行在线状态无关',
    description VARCHAR(512) NOT NULL DEFAULT '',
    legacy_ref VARCHAR(64) NULL COMMENT '旧四遥表数字 ID',
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (id),
    UNIQUE KEY uq_device_id (device_id),
    UNIQUE KEY uq_device_path (path),
    UNIQUE KEY uq_device_sibling (parent_device_id, code),
    KEY ix_device_parent (parent_device_id, sort_order),
    KEY ix_device_kind (node_kind, enabled),
    KEY ix_device_source (source_device_id),
    KEY ix_device_type (device_type_code),
    CONSTRAINT ck_device_depth CHECK (depth <= 8),
    CONSTRAINT ck_device_component CHECK (
        node_kind <> 'component' OR source_device_id <> ''
    ),
    CONSTRAINT ck_device_device_type CHECK (
        node_kind IN ('station', 'unit') OR (device_type_code <> '' AND type_version <> '')
    )
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '设备树（站/单元/设备/部件）';

CREATE TABLE IF NOT EXISTS cfg_device_type (
    type_code VARCHAR(32) NOT NULL,
    type_version VARCHAR(16) NOT NULL,
    name VARCHAR(64) NOT NULL,
    category VARCHAR(32) NOT NULL DEFAULT 'device'
        COMMENT 'pcs/bms/hvac/env/fire/meter/relay/battery/rack/pack/cell',
    status ENUM('active', 'retired') NOT NULL DEFAULT 'active',
    description VARCHAR(512) NOT NULL DEFAULT '',
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (type_code, type_version)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '设备类型（含类型版本）';

CREATE TABLE IF NOT EXISTS cfg_type_point (
    type_code VARCHAR(32) NOT NULL,
    type_version VARCHAR(16) NOT NULL,
    semantic_code VARCHAR(64) NOT NULL COMMENT '如 active_power、run_state、start',
    name VARCHAR(128) NOT NULL DEFAULT '',
    point_type ENUM('yc', 'yx', 'yk', 'yt') NOT NULL COMMENT '决定映射行必须落在哪张点表',
    value_type ENUM('bool', 'int64', 'uint64', 'double', 'string') NOT NULL,
    unit VARCHAR(32) NOT NULL DEFAULT '',
    required TINYINT(1) NOT NULL DEFAULT 1 COMMENT '属于任一能力即为必需',
    capability_codes_json JSON NOT NULL COMMENT '所属能力编码数组，可属多个能力',
    writable TINYINT(1) NOT NULL DEFAULT 0,
    stale_after_ms INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 表示继承站默认',
    sort_order INT NOT NULL DEFAULT 0,
    PRIMARY KEY (type_code, type_version, semantic_code),
    KEY ix_type_point_cap (type_code, type_version),
    CONSTRAINT fk_type_point_type FOREIGN KEY (type_code, type_version)
        REFERENCES cfg_device_type (type_code, type_version) ON DELETE CASCADE,
    CONSTRAINT ck_type_point_writable CHECK (point_type IN ('yk', 'yt') OR writable = 0)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '设备类型固有点目录（四遥）';

CREATE TABLE IF NOT EXISTS cfg_template (
    template_id VARCHAR(64) NOT NULL COMMENT '如 PCS-125K',
    template_version VARCHAR(16) NOT NULL COMMENT '如 v2.3',
    device_type_code VARCHAR(32) NOT NULL,
    type_version VARCHAR(16) NOT NULL,
    vendor VARCHAR(64) NOT NULL DEFAULT '',
    model VARCHAR(64) NOT NULL DEFAULT '',
    status ENUM('active', 'retired') NOT NULL DEFAULT 'active',
    points_json JSON NOT NULL COMMENT '默认四遥点表与映射，复制源，不参与运行期联表',
    description VARCHAR(512) NOT NULL DEFAULT '',
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (template_id, template_version),
    KEY ix_template_type (device_type_code, type_version),
    CONSTRAINT fk_template_type FOREIGN KEY (device_type_code, type_version)
        REFERENCES cfg_device_type (type_code, type_version)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '型号模板';

-- ===========================================================================
-- 二、通道与绑定
-- ===========================================================================

CREATE TABLE IF NOT EXISTS cfg_channel (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    channel_id VARCHAR(64) NOT NULL,
    code VARCHAR(64) NOT NULL,
    name VARCHAR(128) NOT NULL DEFAULT '',
    protocol VARCHAR(32) NOT NULL COMMENT 'modbus/iec104/dlt645/iec61850',
    conn_mode ENUM('tcp_client', 'tcp_server', 'serial', 'udp') NOT NULL,
    endpoint VARCHAR(160) NOT NULL DEFAULT '' COMMENT 'host:port 或 COM3',
    serial_json JSON NULL COMMENT '波特率/数据位/停止位/校验',
    poll_interval_ms INT UNSIGNED NOT NULL DEFAULT 1000,
    timeout_ms INT UNSIGNED NOT NULL DEFAULT 3000,
    retry_times TINYINT UNSIGNED NOT NULL DEFAULT 1,
    max_sessions SMALLINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '允许并发的绑定数，表达共享连接',
    enabled TINYINT(1) NOT NULL DEFAULT 1,
    description VARCHAR(512) NOT NULL DEFAULT '',
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (id),
    UNIQUE KEY uq_channel_id (channel_id),
    UNIQUE KEY uq_channel_code (code),
    CONSTRAINT ck_channel_timing CHECK (poll_interval_ms > 0 AND timeout_ms > 0)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '通道（一条通道即一条物理连接）';

CREATE TABLE IF NOT EXISTS cfg_binding (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    binding_id VARCHAR(64) NOT NULL,
    device_id VARCHAR(64) NOT NULL,
    channel_id VARCHAR(64) NOT NULL,
    device_address VARCHAR(64) NOT NULL DEFAULT '' COMMENT 'Unit ID / 公共地址 / 从站号',
    profile_version VARCHAR(32) NOT NULL DEFAULT '' COMMENT '如 mbtcp-v3',
    can_collect TINYINT(1) NOT NULL DEFAULT 0,
    can_control TINYINT(1) NOT NULL DEFAULT 0,
    can_forward TINYINT(1) NOT NULL DEFAULT 0,
    enabled TINYINT(1) NOT NULL DEFAULT 1,
    description VARCHAR(512) NOT NULL DEFAULT '',
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (id),
    UNIQUE KEY uq_binding_id (binding_id),
    UNIQUE KEY uq_binding_pair (device_id, channel_id),
    KEY ix_binding_channel (channel_id),
    CONSTRAINT fk_binding_device FOREIGN KEY (device_id) REFERENCES cfg_device (device_id)
        ON DELETE CASCADE,
    CONSTRAINT fk_binding_channel FOREIGN KEY (channel_id) REFERENCES cfg_channel (channel_id)
        ON DELETE CASCADE,
    CONSTRAINT ck_binding_role CHECK (can_collect + can_control + can_forward > 0)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '设备-通道绑定与角色标志';

-- ===========================================================================
-- 三、四遥点：四张表
--
-- 公共列在四表重复；类型专有列只出现在本类型表中，因此不存在
-- “遥测点带 write_mode”这类可空废列。
-- ===========================================================================

CREATE TABLE IF NOT EXISTS cfg_point_yc (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    point_id VARCHAR(64) NOT NULL COMMENT '稳定点 ID，四表之间也全局唯一（服务校验）',
    owner_device_id VARCHAR(64) NOT NULL COMMENT '点归属节点；部件点填 rack/pack/cell',
    binding_id VARCHAR(64) NOT NULL COMMENT '实际采集来源绑定；部件点填 BMS 的绑定',
    code VARCHAR(64) NOT NULL,
    name VARCHAR(128) NOT NULL DEFAULT '',
    semantic_code VARCHAR(64) NOT NULL DEFAULT '' COMMENT '非空=固有点映射；空=厂家扩展点',
    vendor_key VARCHAR(120) NOT NULL DEFAULT '' COMMENT '厂家点稳定键，重复导入据此更新',
    origin ENUM('type', 'template', 'vendor', 'component') NOT NULL DEFAULT 'vendor',
    address_space ENUM('coil', 'discrete_input', 'input_register', 'holding_register')
        NULL COMMENT '空=非寄存器协议',
    address_offset INT UNSIGNED NOT NULL DEFAULT 0,
    bit_offset TINYINT UNSIGNED NOT NULL DEFAULT 0,
    register_count TINYINT UNSIGNED NOT NULL DEFAULT 1,
    protocol_address_json JSON NULL COMMENT 'IEC104 casdu/ioa、DLT645 data_id、IEC61850 object_ref',
    wire_type ENUM('bool', 'i16', 'u16', 'i32', 'u32', 'f32', 'i64', 'u64', 'f64') NOT NULL,
    byte_order ENUM('ab', 'ba') NULL COMMENT '空=单寄存器',
    value_type ENUM('double', 'int64', 'uint64') NOT NULL DEFAULT 'double',
    unit VARCHAR(32) NOT NULL DEFAULT '',
    decimals TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 表示按值类型默认',
    scale_factor DECIMAL(20, 8) NOT NULL DEFAULT 1,
    offset_value DECIMAL(20, 8) NOT NULL DEFAULT 0,
    range_min DOUBLE NOT NULL DEFAULT -9999999,
    range_max DOUBLE NOT NULL DEFAULT 9999999,
    deadband DOUBLE NOT NULL DEFAULT 0 COMMENT '变化死区，0 表示逐点上报',
    enabled TINYINT(1) NOT NULL DEFAULT 1,
    disabled_reason VARCHAR(512) NOT NULL DEFAULT '',
    stale_after_ms INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 表示继承站默认',
    source_import_row_id VARCHAR(64) NOT NULL DEFAULT '',
    legacy_ref VARCHAR(64) NULL,
    description VARCHAR(512) NOT NULL DEFAULT '',
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (id),
    UNIQUE KEY uq_yc_point_id (point_id),
    UNIQUE KEY uq_yc_code (owner_device_id, code),
    UNIQUE KEY uq_yc_semantic (owner_device_id, (NULLIF(semantic_code, ''))),
    UNIQUE KEY uq_yc_vendor_key (binding_id, (NULLIF(vendor_key, ''))),
    KEY ix_yc_owner (owner_device_id, code),
    KEY ix_yc_binding (binding_id),
    KEY ix_yc_address (binding_id, address_space, address_offset),
    CONSTRAINT fk_yc_device FOREIGN KEY (owner_device_id) REFERENCES cfg_device (device_id)
        ON DELETE CASCADE,
    CONSTRAINT fk_yc_binding FOREIGN KEY (binding_id) REFERENCES cfg_binding (binding_id)
        ON DELETE CASCADE,
    CONSTRAINT ck_yc_scale CHECK (scale_factor <> 0),
    CONSTRAINT ck_yc_range CHECK (range_max >= range_min),
    CONSTRAINT ck_yc_reason CHECK (enabled = 1 OR CHAR_LENGTH(disabled_reason) > 0)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '遥测点（YC）';

CREATE TABLE IF NOT EXISTS cfg_point_yx (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    point_id VARCHAR(64) NOT NULL,
    owner_device_id VARCHAR(64) NOT NULL,
    binding_id VARCHAR(64) NOT NULL,
    code VARCHAR(64) NOT NULL,
    name VARCHAR(128) NOT NULL DEFAULT '',
    semantic_code VARCHAR(64) NOT NULL DEFAULT '',
    vendor_key VARCHAR(120) NOT NULL DEFAULT '',
    origin ENUM('type', 'template', 'vendor', 'component') NOT NULL DEFAULT 'vendor',
    address_space ENUM('coil', 'discrete_input', 'input_register', 'holding_register') NULL,
    address_offset INT UNSIGNED NOT NULL DEFAULT 0,
    bit_offset TINYINT UNSIGNED NOT NULL DEFAULT 0,
    register_count TINYINT UNSIGNED NOT NULL DEFAULT 1,
    protocol_address_json JSON NULL,
    wire_type ENUM('bool', 'i16', 'u16', 'i32', 'u32', 'f32', 'i64', 'u64', 'f64') NOT NULL,
    byte_order ENUM('ab', 'ba') NULL,
    value_type ENUM('bool', 'int64', 'string') NOT NULL DEFAULT 'bool',
    unit VARCHAR(32) NOT NULL DEFAULT '',
    soe TINYINT(1) NOT NULL DEFAULT 0 COMMENT '记录变位；事件不是第五类遥，仍属遥信',
    invert TINYINT(1) NOT NULL DEFAULT 0 COMMENT '原始位取反（常闭接点）',
    enum_json JSON NULL COMMENT '[{raw,label,severity}]',
    unknown_policy ENUM('retain_raw', 'uncertain', 'invalid') NOT NULL DEFAULT 'retain_raw',
    feedback_of_point_id VARCHAR(64) NOT NULL DEFAULT '' COMMENT '反馈点指回遥控/遥调点',
    enabled TINYINT(1) NOT NULL DEFAULT 1,
    disabled_reason VARCHAR(512) NOT NULL DEFAULT '',
    stale_after_ms INT UNSIGNED NOT NULL DEFAULT 0,
    source_import_row_id VARCHAR(64) NOT NULL DEFAULT '',
    legacy_ref VARCHAR(64) NULL,
    description VARCHAR(512) NOT NULL DEFAULT '',
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (id),
    UNIQUE KEY uq_yx_point_id (point_id),
    UNIQUE KEY uq_yx_code (owner_device_id, code),
    UNIQUE KEY uq_yx_semantic (owner_device_id, (NULLIF(semantic_code, ''))),
    UNIQUE KEY uq_yx_vendor_key (binding_id, (NULLIF(vendor_key, ''))),
    KEY ix_yx_owner (owner_device_id, code),
    KEY ix_yx_binding (binding_id),
    KEY ix_yx_address (binding_id, address_space, address_offset),
    KEY ix_yx_feedback (feedback_of_point_id),
    CONSTRAINT fk_yx_device FOREIGN KEY (owner_device_id) REFERENCES cfg_device (device_id)
        ON DELETE CASCADE,
    CONSTRAINT fk_yx_binding FOREIGN KEY (binding_id) REFERENCES cfg_binding (binding_id)
        ON DELETE CASCADE,
    CONSTRAINT ck_yx_reason CHECK (enabled = 1 OR CHAR_LENGTH(disabled_reason) > 0)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '遥信点（YX）';

CREATE TABLE IF NOT EXISTS cfg_point_yk (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    point_id VARCHAR(64) NOT NULL,
    owner_device_id VARCHAR(64) NOT NULL,
    binding_id VARCHAR(64) NOT NULL,
    code VARCHAR(64) NOT NULL,
    name VARCHAR(128) NOT NULL DEFAULT '',
    semantic_code VARCHAR(64) NOT NULL DEFAULT '',
    vendor_key VARCHAR(120) NOT NULL DEFAULT '',
    origin ENUM('type', 'template', 'vendor', 'component') NOT NULL DEFAULT 'vendor',
    address_space ENUM('coil', 'discrete_input', 'input_register', 'holding_register') NULL,
    address_offset INT UNSIGNED NOT NULL DEFAULT 0,
    bit_offset TINYINT UNSIGNED NOT NULL DEFAULT 0,
    register_count TINYINT UNSIGNED NOT NULL DEFAULT 1,
    protocol_address_json JSON NULL,
    wire_type ENUM('bool', 'i16', 'u16', 'i32', 'u32', 'f32', 'i64', 'u64', 'f64') NOT NULL,
    byte_order ENUM('ab', 'ba') NULL,
    value_type ENUM('bool', 'int64') NOT NULL DEFAULT 'bool',
    unit VARCHAR(32) NOT NULL DEFAULT '',
    write_mode ENUM('fixed', 'direct') NOT NULL DEFAULT 'direct',
    fixed_raw_value BIGINT NOT NULL DEFAULT 0 COMMENT 'fixed 模式必写',
    pulse_width_ms INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 表示保持',
    min_interval_ms INT UNSIGNED NOT NULL DEFAULT 0,
    write_group_key VARCHAR(64) NOT NULL DEFAULT '' COMMENT '同组写寄存器互斥',
    feedback_json JSON NULL COMMENT '[{point_code,criterion,expected,tolerance,order,required}]',
    feedback_timeout_ms INT UNSIGNED NOT NULL DEFAULT 5000,
    failure_policy ENUM('reject', 'retry', 'escalate') NOT NULL DEFAULT 'reject',
    enabled TINYINT(1) NOT NULL DEFAULT 1,
    disabled_reason VARCHAR(512) NOT NULL DEFAULT '',
    stale_after_ms INT UNSIGNED NOT NULL DEFAULT 0,
    source_import_row_id VARCHAR(64) NOT NULL DEFAULT '',
    legacy_ref VARCHAR(64) NULL,
    description VARCHAR(512) NOT NULL DEFAULT '',
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (id),
    UNIQUE KEY uq_yk_point_id (point_id),
    UNIQUE KEY uq_yk_code (owner_device_id, code),
    UNIQUE KEY uq_yk_semantic (owner_device_id, (NULLIF(semantic_code, ''))),
    UNIQUE KEY uq_yk_vendor_key (binding_id, (NULLIF(vendor_key, ''))),
    KEY ix_yk_owner (owner_device_id, code),
    KEY ix_yk_binding (binding_id),
    KEY ix_yk_address (binding_id, address_space, address_offset),
    KEY ix_yk_group (binding_id, write_group_key),
    CONSTRAINT fk_yk_device FOREIGN KEY (owner_device_id) REFERENCES cfg_device (device_id)
        ON DELETE CASCADE,
    CONSTRAINT fk_yk_binding FOREIGN KEY (binding_id) REFERENCES cfg_binding (binding_id)
        ON DELETE CASCADE,
    CONSTRAINT ck_yk_reason CHECK (enabled = 1 OR CHAR_LENGTH(disabled_reason) > 0)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '遥控点（YK）';

CREATE TABLE IF NOT EXISTS cfg_point_yt (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    point_id VARCHAR(64) NOT NULL,
    owner_device_id VARCHAR(64) NOT NULL,
    binding_id VARCHAR(64) NOT NULL,
    code VARCHAR(64) NOT NULL,
    name VARCHAR(128) NOT NULL DEFAULT '',
    semantic_code VARCHAR(64) NOT NULL DEFAULT '',
    vendor_key VARCHAR(120) NOT NULL DEFAULT '',
    origin ENUM('type', 'template', 'vendor', 'component') NOT NULL DEFAULT 'vendor',
    address_space ENUM('coil', 'discrete_input', 'input_register', 'holding_register') NULL,
    address_offset INT UNSIGNED NOT NULL DEFAULT 0,
    bit_offset TINYINT UNSIGNED NOT NULL DEFAULT 0,
    register_count TINYINT UNSIGNED NOT NULL DEFAULT 1,
    protocol_address_json JSON NULL,
    wire_type ENUM('bool', 'i16', 'u16', 'i32', 'u32', 'f32', 'i64', 'u64', 'f64') NOT NULL,
    byte_order ENUM('ab', 'ba') NULL,
    value_type ENUM('double', 'int64') NOT NULL DEFAULT 'double',
    unit VARCHAR(32) NOT NULL DEFAULT '',
    decimals TINYINT UNSIGNED NOT NULL DEFAULT 0,
    scale_factor DECIMAL(20, 8) NOT NULL DEFAULT 1 COMMENT '原码 = (工程值 - offset) / scale',
    offset_value DECIMAL(20, 8) NOT NULL DEFAULT 0,
    range_min DOUBLE NOT NULL DEFAULT -9999999 COMMENT '可写工程值下限',
    range_max DOUBLE NOT NULL DEFAULT 9999999 COMMENT '可写工程值上限',
    min_interval_ms INT UNSIGNED NOT NULL DEFAULT 0,
    write_group_key VARCHAR(64) NOT NULL DEFAULT '',
    feedback_json JSON NULL,
    feedback_timeout_ms INT UNSIGNED NOT NULL DEFAULT 5000,
    failure_policy ENUM('reject', 'retry', 'escalate') NOT NULL DEFAULT 'reject',
    enabled TINYINT(1) NOT NULL DEFAULT 1,
    disabled_reason VARCHAR(512) NOT NULL DEFAULT '',
    stale_after_ms INT UNSIGNED NOT NULL DEFAULT 0,
    source_import_row_id VARCHAR(64) NOT NULL DEFAULT '',
    legacy_ref VARCHAR(64) NULL,
    description VARCHAR(512) NOT NULL DEFAULT '',
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (id),
    UNIQUE KEY uq_yt_point_id (point_id),
    UNIQUE KEY uq_yt_code (owner_device_id, code),
    UNIQUE KEY uq_yt_semantic (owner_device_id, (NULLIF(semantic_code, ''))),
    UNIQUE KEY uq_yt_vendor_key (binding_id, (NULLIF(vendor_key, ''))),
    KEY ix_yt_owner (owner_device_id, code),
    KEY ix_yt_binding (binding_id),
    KEY ix_yt_address (binding_id, address_space, address_offset),
    KEY ix_yt_group (binding_id, write_group_key),
    CONSTRAINT fk_yt_device FOREIGN KEY (owner_device_id) REFERENCES cfg_device (device_id)
        ON DELETE CASCADE,
    CONSTRAINT fk_yt_binding FOREIGN KEY (binding_id) REFERENCES cfg_binding (binding_id)
        ON DELETE CASCADE,
    CONSTRAINT ck_yt_scale CHECK (scale_factor <> 0),
    CONSTRAINT ck_yt_range CHECK (range_max >= range_min),
    CONSTRAINT ck_yt_reason CHECK (enabled = 1 OR CHAR_LENGTH(disabled_reason) > 0)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '遥调点（YT）';

-- ===========================================================================
-- 四、点表导入
-- ===========================================================================

CREATE TABLE IF NOT EXISTS cfg_import_job (
    import_job_id VARCHAR(64) NOT NULL,
    device_id VARCHAR(64) NOT NULL COMMENT '目标通信设备',
    binding_id VARCHAR(64) NOT NULL COMMENT '目标采集/下发绑定',
    source_kind ENUM('vendor_csv', 'vendor_xlsx', 'telemetry_xlsx') NOT NULL,
    file_name VARCHAR(255) NOT NULL DEFAULT '',
    file_sha256 CHAR(64) NOT NULL,
    file_bytes INT UNSIGNED NOT NULL DEFAULT 0,
    address_base ENUM('zero', 'one') NOT NULL DEFAULT 'zero' COMMENT '十进制地址基准',
    status ENUM('parsed', 'confirmed', 'applied', 'failed') NOT NULL DEFAULT 'parsed',
    row_total INT UNSIGNED NOT NULL DEFAULT 0,
    row_ok INT UNSIGNED NOT NULL DEFAULT 0,
    row_warning INT UNSIGNED NOT NULL DEFAULT 0,
    row_error INT UNSIGNED NOT NULL DEFAULT 0,
    idempotency_key CHAR(64) NOT NULL COMMENT 'SHA-256(device|binding|sha256)',
    submitted_by VARCHAR(64) NOT NULL DEFAULT '',
    submitted_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    finished_at DATETIME(6) NULL,
    error_code VARCHAR(64) NOT NULL DEFAULT '',
    error_message VARCHAR(1024) NOT NULL DEFAULT '',
    PRIMARY KEY (import_job_id),
    UNIQUE KEY uq_import_idem (idempotency_key),
    KEY ix_import_target (device_id, submitted_at)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '点表导入任务与幂等键';

CREATE TABLE IF NOT EXISTS cfg_import_row (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    import_job_id VARCHAR(64) NOT NULL,
    line_no INT UNSIGNED NOT NULL,
    vendor_key VARCHAR(120) NOT NULL DEFAULT '',
    vendor_name VARCHAR(128) NOT NULL DEFAULT '',
    point_type ENUM('yc', 'yx', 'yk', 'yt') NOT NULL COMMENT '决定写入哪张点表',
    target_device_path VARCHAR(512) NOT NULL DEFAULT '' COMMENT '空即“目标节点缺失”',
    semantic_code VARCHAR(64) NOT NULL DEFAULT '',
    address_space ENUM('coil', 'discrete_input', 'input_register', 'holding_register') NULL,
    address_offset INT UNSIGNED NOT NULL DEFAULT 0,
    bit_offset TINYINT UNSIGNED NOT NULL DEFAULT 0,
    register_count TINYINT UNSIGNED NOT NULL DEFAULT 1,
    protocol_address_json JSON NULL,
    wire_type VARCHAR(16) NOT NULL,
    byte_order VARCHAR(8) NOT NULL DEFAULT '',
    value_type VARCHAR(16) NOT NULL DEFAULT '',
    scale_factor DECIMAL(20, 8) NOT NULL DEFAULT 1,
    offset_value DECIMAL(20, 8) NOT NULL DEFAULT 0,
    unit VARCHAR(32) NOT NULL DEFAULT '',
    parse_status ENUM('ok', 'warning', 'error') NOT NULL DEFAULT 'ok',
    parse_code VARCHAR(64) NOT NULL DEFAULT '',
    parse_message VARCHAR(1024) NOT NULL DEFAULT '',
    confirm_state ENUM('none', 'accepted', 'fixed', 'rejected') NOT NULL DEFAULT 'none',
    confirm_note VARCHAR(512) NOT NULL DEFAULT '',
    confirm_by VARCHAR(64) NOT NULL DEFAULT '',
    confirm_at DATETIME(6) NULL,
    resolved_point_id VARCHAR(64) NOT NULL DEFAULT '',
    PRIMARY KEY (id),
    UNIQUE KEY uq_import_row (import_job_id, line_no),
    KEY ix_import_row_key (import_job_id, vendor_key),
    CONSTRAINT fk_import_row_job FOREIGN KEY (import_job_id)
        REFERENCES cfg_import_job (import_job_id) ON DELETE CASCADE
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '导入逐行解析与逐项确认';

-- ===========================================================================
-- 五、运行统计（最新状态仍在实时库，这两张表只做追溯）
-- ===========================================================================

CREATE TABLE IF NOT EXISTS ops_comm_stat_5m (
    binding_id VARCHAR(64) NOT NULL,
    window_start DATETIME(6) NOT NULL,
    request_total BIGINT UNSIGNED NOT NULL DEFAULT 0,
    success_total BIGINT UNSIGNED NOT NULL DEFAULT 0,
    timeout_total BIGINT UNSIGNED NOT NULL DEFAULT 0,
    error_total BIGINT UNSIGNED NOT NULL DEFAULT 0,
    avg_latency_ms INT UNSIGNED NULL,
    p95_latency_ms INT UNSIGNED NULL,
    last_valid_at DATETIME(6) NULL,
    last_error_code VARCHAR(64) NOT NULL DEFAULT '',
    PRIMARY KEY (binding_id, window_start),
    KEY ix_comm_stat_window (window_start)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '绑定维度 5 分钟通信统计';

CREATE TABLE IF NOT EXISTS ops_state_log (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    object_kind ENUM('device', 'channel', 'binding') NOT NULL,
    object_id VARCHAR(64) NOT NULL,
    state VARCHAR(32) NOT NULL COMMENT 'unconfigured/online/alarm/fault/offline',
    since DATETIME(6) NOT NULL,
    reason VARCHAR(255) NOT NULL DEFAULT '',
    PRIMARY KEY (id),
    KEY ix_state_object (object_kind, object_id, since),
    KEY ix_state_window (since)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '设备/通道/绑定状态变化历史';

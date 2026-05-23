/**
 * @file point_config_v2.sql
 * @brief 测点配置表 (V2) — 兼容 DLT645 / MODBUS / IEC104 / IEC61850
 *
 * 四遥各自独立建表，每张表包含完整的协议地址字段。
 * 各协议选用对应字段:
 *   MODBUS:  protocol_type=1/2,  slave_id, reg_addr, bit_position
 *   DLT645:  protocol_type=0,    slave_id, data_id
 *   IEC104:  protocol_type=3,    casdu, ioa
 *   IEC61850:protocol_type=4,    object_ref
 */

-- ============================================================
-- 遥测配置表 (YC)
-- ============================================================
CREATE TABLE point_yc (
    id                INTEGER      NOT NULL AUTO_INCREMENT,
    code              VARCHAR(128) NOT NULL,  -- 测点编码 (全局唯一)
    name              VARCHAR(128),            -- 测点名称
    channel_id        INTEGER,                 -- 通道 ID
    device_id         INTEGER,                 -- 设备 ID

    -- 协议地址 (通用)
    protocol_type     TINYINT      DEFAULT 1,  -- 0=DLT645, 1=MODBUS_RTU, 2=MODBUS_TCP, 3=IEC104, 4=IEC61850
    slave_id          TINYINT      DEFAULT 1,  -- 从站地址
    reg_addr          INT          DEFAULT 0,  -- 寄存器地址
    bit_position      TINYINT      DEFAULT 0,  -- 位号 (0=整寄存器)

    -- DLT645
    data_id           INT UNSIGNED DEFAULT 0,  -- 数据标识 DI3-DI0

    -- IEC104
    casdu             SMALLINT     DEFAULT 0,  -- 公共地址
    ioa               INT UNSIGNED DEFAULT 0,  -- 信息体地址

    -- IEC61850
    object_ref        VARCHAR(256),            -- 对象引用路径

    -- 解析码
    decode_code       TINYINT      DEFAULT 0,  -- 见 DecodeCode 枚举: 0=UINT16, ... 6=FLOAT...

    -- 工程值变换
    mul_coe           DOUBLE       DEFAULT 1.0,        -- 乘法系数
    add_coe           DOUBLE       DEFAULT 0.0,        -- 加法系数
    min_limit         DOUBLE       DEFAULT -9999999.0, -- 下限
    max_limit         DOUBLE       DEFAULT 9999999.0,  -- 上限
    unit              VARCHAR(32),                     -- 工程单位

    -- 元数据
    enable            BOOLEAN      DEFAULT 1   NOT NULL,
    description       VARCHAR(256),
    created_at        DATETIME     DEFAULT CURRENT_TIMESTAMP,
    updated_at        DATETIME     DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (id),
    UNIQUE KEY uq_point_yc_code (code),
    INDEX ix_point_yc_channel (channel_id),
    INDEX ix_point_yc_device (device_id)
);

-- ============================================================
-- 遥信配置表 (YX)
-- ============================================================
CREATE TABLE point_yx (
    id                INTEGER      NOT NULL AUTO_INCREMENT,
    code              VARCHAR(128) NOT NULL,
    name              VARCHAR(128),
    channel_id        INTEGER,
    device_id         INTEGER,

    -- 协议地址
    protocol_type     TINYINT      DEFAULT 1,
    slave_id          TINYINT      DEFAULT 1,
    reg_addr          INT          DEFAULT 0,
    bit_position      TINYINT      DEFAULT 0,

    data_id           INT UNSIGNED DEFAULT 0,
    casdu             SMALLINT     DEFAULT 0,
    ioa               INT UNSIGNED DEFAULT 0,
    object_ref        VARCHAR(256),

    -- 解析码
    decode_code       TINYINT      DEFAULT 0,

    -- 遥信属性
    bit               TINYINT      DEFAULT 0,  -- 位号 (0=整寄存器)
    reverse_flag      BOOLEAN      DEFAULT 0,  -- 是否取反

    -- 元数据
    enable            BOOLEAN      DEFAULT 1   NOT NULL,
    description       VARCHAR(256),
    created_at        DATETIME     DEFAULT CURRENT_TIMESTAMP,
    updated_at        DATETIME     DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (id),
    UNIQUE KEY uq_point_yx_code (code),
    INDEX ix_point_yx_channel (channel_id),
    INDEX ix_point_yx_device (device_id)
);

-- ============================================================
-- 遥控配置表 (YK)
-- ============================================================
CREATE TABLE point_yk (
    id                INTEGER      NOT NULL AUTO_INCREMENT,
    code              VARCHAR(128) NOT NULL,
    name              VARCHAR(128),
    channel_id        INTEGER,
    device_id         INTEGER,

    -- 协议地址
    protocol_type     TINYINT      DEFAULT 1,
    slave_id          TINYINT      DEFAULT 1,
    reg_addr          INT          DEFAULT 0,
    bit_position      TINYINT      DEFAULT 0,

    data_id           INT UNSIGNED DEFAULT 0,
    casdu             SMALLINT     DEFAULT 0,
    ioa               INT UNSIGNED DEFAULT 0,
    object_ref        VARCHAR(256),

    -- 解析码
    decode_code       TINYINT      DEFAULT 0,

    -- 遥控属性
    bit               TINYINT      DEFAULT 0,   -- 位号
    command_type      TINYINT      DEFAULT 0,   -- 命令类型 (0=选择, 1=执行)
    related_yx_id     INTEGER,                  -- 关联遥信 ID (返校用)

    -- 元数据
    enable            BOOLEAN      DEFAULT 1   NOT NULL,
    description       VARCHAR(256),
    created_at        DATETIME     DEFAULT CURRENT_TIMESTAMP,
    updated_at        DATETIME     DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (id),
    UNIQUE KEY uq_point_yk_code (code),
    INDEX ix_point_yk_channel (channel_id),
    INDEX ix_point_yk_device (device_id)
);

-- ============================================================
-- 遥调配置表 (YT)
-- ============================================================
CREATE TABLE point_yt (
    id                INTEGER      NOT NULL AUTO_INCREMENT,
    code              VARCHAR(128) NOT NULL,
    name              VARCHAR(128),
    channel_id        INTEGER,
    device_id         INTEGER,

    -- 协议地址
    protocol_type     TINYINT      DEFAULT 1,
    slave_id          TINYINT      DEFAULT 1,
    reg_addr          INT          DEFAULT 0,
    bit_position      TINYINT      DEFAULT 0,

    data_id           INT UNSIGNED DEFAULT 0,
    casdu             SMALLINT     DEFAULT 0,
    ioa               INT UNSIGNED DEFAULT 0,
    object_ref        VARCHAR(256),

    -- 解析码
    decode_code       TINYINT      DEFAULT 0,

    -- 遥调属性
    mul_coe           DOUBLE       DEFAULT 1.0,
    add_coe           DOUBLE       DEFAULT 0.0,
    min_limit         DOUBLE       DEFAULT -9999999.0,
    max_limit         DOUBLE       DEFAULT 9999999.0,
    unit              VARCHAR(32),

    -- 元数据
    enable            BOOLEAN      DEFAULT 1   NOT NULL,
    description       VARCHAR(256),
    created_at        DATETIME     DEFAULT CURRENT_TIMESTAMP,
    updated_at        DATETIME     DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (id),
    UNIQUE KEY uq_point_yt_code (code),
    INDEX ix_point_yt_channel (channel_id),
    INDEX ix_point_yt_device (device_id)
);

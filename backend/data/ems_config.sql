-- ============================================================
-- EMS 配置数据库建表脚本
-- 设备表、通道表、四遥测点配置表
-- 兼容 MySQL / SQLite
-- ============================================================

-- ============================================================
-- 设备表 (device)
-- ============================================================
CREATE TABLE IF NOT EXISTS device (
    id          INTEGER      NOT NULL AUTO_INCREMENT,
    code        VARCHAR(128) NOT NULL,           -- 设备编码 (全局唯一)
    name        VARCHAR(128),                     -- 设备名称
    device_type TINYINT      DEFAULT 0  NOT NULL, -- 设备类型: 0=RTU,1=Protection,2=Meter,3=IED,4=PLC,5=Virtual,6=Gateway,99=Other
    group_id    INTEGER      DEFAULT 0,           -- 所属设备组ID
    description VARCHAR(256),                     -- 描述
    enable      BOOLEAN      DEFAULT 1  NOT NULL, -- 是否启用
    created_at  DATETIME     DEFAULT CURRENT_TIMESTAMP,
    updated_at  DATETIME     DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (id),
    UNIQUE KEY uq_device_code (code),
    INDEX ix_device_group (group_id)
);

-- ============================================================
-- 通道表 (channel)
-- ============================================================
CREATE TABLE IF NOT EXISTS channel (
    id            INTEGER      NOT NULL AUTO_INCREMENT,
    code          VARCHAR(128) NOT NULL,           -- 通道编码 (全局唯一)
    name          VARCHAR(128),                     -- 通道名称
    device_id     INTEGER,                          -- 关联设备ID
    protocol_type TINYINT      DEFAULT 1  NOT NULL, -- 协议类型: 0=DLT645,1=MODBUS_RTU,2=MODBUS_TCP,3=IEC104,4=IEC61850
    conn_type     TINYINT      DEFAULT 1  NOT NULL, -- 连接类型: 0=Serial,1=TCP,2=TCPServer,3=UDP

    -- TCP 参数
    ip            VARCHAR(64),
    port          INTEGER      DEFAULT 502,

    -- 串口参数
    com_port      VARCHAR(32),
    baud_rate     INTEGER      DEFAULT 9600,
    data_bits     INTEGER      DEFAULT 8,
    stop_bits     INTEGER      DEFAULT 1,
    parity        VARCHAR(1)   DEFAULT 'N',

    -- Modbus 参数
    rtu_addr      VARCHAR(16)  DEFAULT '1',        -- RTU 地址 (逗号分隔支持多个)
    timeout       INTEGER      DEFAULT 5,           -- 超时时间(秒)

    -- 元数据
    enable        BOOLEAN      DEFAULT 1  NOT NULL,
    created_at    DATETIME     DEFAULT CURRENT_TIMESTAMP,
    updated_at    DATETIME     DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (id),
    UNIQUE KEY uq_channel_code (code),
    INDEX ix_channel_device (device_id)
);

-- ============================================================
-- 遥测配置表 (YC)
-- ============================================================
CREATE TABLE IF NOT EXISTS point_yc (
    id                INTEGER      NOT NULL AUTO_INCREMENT,
    code              VARCHAR(128) NOT NULL,         -- 测点编码 (全局唯一)
    name              VARCHAR(128),                  -- 测点名称
    channel_id        INTEGER,                       -- 所属通道ID
    device_id         INTEGER,                       -- 所属设备ID

    -- 协议地址 (通用)
    protocol_type     TINYINT      DEFAULT 1,        -- 0=DLT645,1=MODBUS_RTU,2=MODBUS_TCP,3=IEC104,4=IEC61850
    slave_id          TINYINT      DEFAULT 1,        -- 从站地址
    reg_addr          INT          DEFAULT 0,        -- 寄存器地址
    bit_position      TINYINT      DEFAULT 0,        -- 位号 (0=整寄存器)

    -- DLT645
    data_id           INT UNSIGNED DEFAULT 0,        -- 数据标识 DI3-DI0

    -- IEC104
    casdu             SMALLINT     DEFAULT 0,        -- 公共地址
    ioa               INT UNSIGNED DEFAULT 0,        -- 信息体地址

    -- IEC61850
    object_ref        VARCHAR(256),                  -- 对象引用路径

    -- 解析码 & 系数
    decode_code       TINYINT      DEFAULT 0,        -- 解析码枚举: 0=UINT16...
    mul_coe           DOUBLE       DEFAULT 1.0,      -- 乘法系数
    add_coe           DOUBLE       DEFAULT 0.0,      -- 加法系数
    min_limit         DOUBLE       DEFAULT -9999999.0,
    max_limit         DOUBLE       DEFAULT 9999999.0,
    unit              VARCHAR(32),                   -- 工程单位

    -- 元数据
    enable            BOOLEAN      DEFAULT 1  NOT NULL,
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
CREATE TABLE IF NOT EXISTS point_yx (
    id                INTEGER      NOT NULL AUTO_INCREMENT,
    code              VARCHAR(128) NOT NULL,
    name              VARCHAR(128),
    channel_id        INTEGER,
    device_id         INTEGER,

    protocol_type     TINYINT      DEFAULT 1,
    slave_id          TINYINT      DEFAULT 1,
    reg_addr          INT          DEFAULT 0,
    bit_position      TINYINT      DEFAULT 0,
    data_id           INT UNSIGNED DEFAULT 0,
    casdu             SMALLINT     DEFAULT 0,
    ioa               INT UNSIGNED DEFAULT 0,
    object_ref        VARCHAR(256),

    decode_code       TINYINT      DEFAULT 0,

    -- 遥信属性
    bit               TINYINT      DEFAULT 0,       -- 位号
    reverse_flag      BOOLEAN      DEFAULT 0,       -- 是否取反

    enable            BOOLEAN      DEFAULT 1  NOT NULL,
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
CREATE TABLE IF NOT EXISTS point_yk (
    id                INTEGER      NOT NULL AUTO_INCREMENT,
    code              VARCHAR(128) NOT NULL,
    name              VARCHAR(128),
    channel_id        INTEGER,
    device_id         INTEGER,

    protocol_type     TINYINT      DEFAULT 1,
    slave_id          TINYINT      DEFAULT 1,
    reg_addr          INT          DEFAULT 0,
    bit_position      TINYINT      DEFAULT 0,
    data_id           INT UNSIGNED DEFAULT 0,
    casdu             SMALLINT     DEFAULT 0,
    ioa               INT UNSIGNED DEFAULT 0,
    object_ref        VARCHAR(256),

    decode_code       TINYINT      DEFAULT 0,

    -- 遥控属性
    bit               TINYINT      DEFAULT 0,       -- 位号
    command_type      TINYINT      DEFAULT 0,       -- 命令类型
    related_yx_id     INTEGER,                      -- 关联遥信 ID (返校用)

    enable            BOOLEAN      DEFAULT 1  NOT NULL,
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
CREATE TABLE IF NOT EXISTS point_yt (
    id                INTEGER      NOT NULL AUTO_INCREMENT,
    code              VARCHAR(128) NOT NULL,
    name              VARCHAR(128),
    channel_id        INTEGER,
    device_id         INTEGER,

    protocol_type     TINYINT      DEFAULT 1,
    slave_id          TINYINT      DEFAULT 1,
    reg_addr          INT          DEFAULT 0,
    bit_position      TINYINT      DEFAULT 0,
    data_id           INT UNSIGNED DEFAULT 0,
    casdu             SMALLINT     DEFAULT 0,
    ioa               INT UNSIGNED DEFAULT 0,
    object_ref        VARCHAR(256),

    decode_code       TINYINT      DEFAULT 0,

    -- 遥调属性
    mul_coe           DOUBLE       DEFAULT 1.0,
    add_coe           DOUBLE       DEFAULT 0.0,
    min_limit         DOUBLE       DEFAULT -9999999.0,
    max_limit         DOUBLE       DEFAULT 9999999.0,
    unit              VARCHAR(32),

    enable            BOOLEAN      DEFAULT 1  NOT NULL,
    description       VARCHAR(256),
    created_at        DATETIME     DEFAULT CURRENT_TIMESTAMP,
    updated_at        DATETIME     DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (id),
    UNIQUE KEY uq_point_yt_code (code),
    INDEX ix_point_yt_channel (channel_id),
    INDEX ix_point_yt_device (device_id)
);

-- ============================================================
-- 测点采集数据表 (历史采集数据)
-- ============================================================
CREATE TABLE IF NOT EXISTS point_collect_data (
    id           BIGINT       NOT NULL AUTO_INCREMENT,
    point_code   VARCHAR(128) NOT NULL,          -- 测点编码
    point_type   TINYINT      NOT NULL,           -- 测点类型: 0=YC,1=YX,2=YK,3=YT
    channel_id   INTEGER      DEFAULT 0,          -- 通道ID
    device_id    INTEGER      DEFAULT 0,          -- 设备ID
    raw_value    BIGINT       DEFAULT 0,          -- 原始报文值
    eng_value    DOUBLE       DEFAULT 0.0,        -- 工程值
    quality      TINYINT      DEFAULT 0,          -- 品质: 0=Good,1=Invalid,2=Reserved,3=Error
    collect_time DATETIME     NOT NULL,            -- 采集时间戳

    PRIMARY KEY (id),
    INDEX ix_collect_point (point_code),
    INDEX ix_collect_channel (channel_id),
    INDEX ix_collect_device (device_id),
    INDEX ix_collect_time (collect_time)
);

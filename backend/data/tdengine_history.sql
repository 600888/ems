-- TDengine 3.4.3.x；由部署显式执行，不由应用启动时创建。
-- ts 是周期归档时刻，measurement_ts 保留原采集时刻。
-- 子表名为站根至测点的 code 冒号路径，如 `station:unit01:bms01:voltage`，最长 192 字节。
-- config_revision 仅为站根热加载计数，不对应配置版本或配置快照。
CREATE DATABASE IF NOT EXISTS ems_history PRECISION 'us' KEEP 365;

CREATE STABLE IF NOT EXISTS ems_history.telemetry_numeric (
    ts TIMESTAMP,
    event_id VARCHAR(64) COMPOSITE KEY,
    sample_value DOUBLE,
    quality INT,
    ingest_ts TIMESTAMP,
    time_origin TINYINT,
    time_reason VARCHAR(128),
    quality_reason VARCHAR(256),
    payload_sha256 VARCHAR(64),
    scope_kind VARCHAR(8),
    scope_id VARCHAR(64),
    config_revision VARCHAR(20),
    source_id VARCHAR(64),
    source_epoch VARCHAR(20),
    sequence VARCHAR(20),
    measurement_ts TIMESTAMP
) TAGS (point_id VARCHAR(64));

CREATE STABLE IF NOT EXISTS ems_history.telemetry_integer (
    ts TIMESTAMP,
    event_id VARCHAR(64) COMPOSITE KEY,
    sample_value BIGINT,
    quality INT,
    ingest_ts TIMESTAMP,
    time_origin TINYINT,
    time_reason VARCHAR(128),
    quality_reason VARCHAR(256),
    payload_sha256 VARCHAR(64),
    scope_kind VARCHAR(8),
    scope_id VARCHAR(64),
    config_revision VARCHAR(20),
    source_id VARCHAR(64),
    source_epoch VARCHAR(20),
    sequence VARCHAR(20),
    measurement_ts TIMESTAMP
) TAGS (point_id VARCHAR(64));

CREATE STABLE IF NOT EXISTS ems_history.telemetry_boolean (
    ts TIMESTAMP,
    event_id VARCHAR(64) COMPOSITE KEY,
    sample_value BOOL,
    quality INT,
    ingest_ts TIMESTAMP,
    time_origin TINYINT,
    time_reason VARCHAR(128),
    quality_reason VARCHAR(256),
    payload_sha256 VARCHAR(64),
    scope_kind VARCHAR(8),
    scope_id VARCHAR(64),
    config_revision VARCHAR(20),
    source_id VARCHAR(64),
    source_epoch VARCHAR(20),
    sequence VARCHAR(20),
    measurement_ts TIMESTAMP
) TAGS (point_id VARCHAR(64));

CREATE STABLE IF NOT EXISTS ems_history.telemetry_text (
    ts TIMESTAMP,
    event_id VARCHAR(64) COMPOSITE KEY,
    sample_value VARCHAR(1024),
    quality INT,
    ingest_ts TIMESTAMP,
    time_origin TINYINT,
    time_reason VARCHAR(128),
    quality_reason VARCHAR(256),
    payload_sha256 VARCHAR(64),
    scope_kind VARCHAR(8),
    scope_id VARCHAR(64),
    config_revision VARCHAR(20),
    source_id VARCHAR(64),
    source_epoch VARCHAR(20),
    sequence VARCHAR(20),
    measurement_ts TIMESTAMP
) TAGS (point_id VARCHAR(64));

-- 告警按站建子表。同一事件保留原时间和稳定 ID，重投使用相同复合主键。
CREATE STABLE IF NOT EXISTS ems_history.alarm_event (
    ts TIMESTAMP,
    event_id VARCHAR(64) COMPOSITE KEY,
    alarm_id VARCHAR(64),
    device_id VARCHAR(64),
    stream_seq BIGINT,
    kind TINYINT,
    payload VARCHAR(50000)
) TAGS (station_id VARCHAR(64));

-- 时间列用 -1 表示未恢复或未确认，原始 JSON 仍以 null 表示。
CREATE STABLE IF NOT EXISTS ems_history.alarm_state (
    ts TIMESTAMP,
    event_id VARCHAR(64) COMPOSITE KEY,
    alarm_id VARCHAR(64),
    device_id VARCHAR(64),
    stream_seq BIGINT,
    occurred_at_us BIGINT,
    cleared_at_us BIGINT,
    acknowledged_at_us BIGINT,
    severity VARCHAR(8),
    payload VARCHAR(50000)
) TAGS (station_id VARCHAR(64));


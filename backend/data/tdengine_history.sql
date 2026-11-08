-- TDengine 3.4.2.x；由部署显式执行，不由应用启动时创建。
-- ts 是周期归档时刻，measurement_ts 保留原采集时刻。
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


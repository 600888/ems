-- Target TDengine 3.4.2 with composite primary key; verify on the pinned patch release.
-- Run with the deployment's measured KEEP and replica policy before production traffic.
CREATE DATABASE IF NOT EXISTS ems_history PRECISION 'us';

CREATE STABLE IF NOT EXISTS ems_history.telemetry_numeric (
    ts TIMESTAMP,
    event_id VARCHAR(64) COMPOSITE KEY,
    sample_value DOUBLE,
    quality INT,
    ingest_ts TIMESTAMP,
    time_origin TINYINT,
    time_reason VARCHAR(64),
    quality_reason VARCHAR(64),
    payload_sha256 VARCHAR(64),
    scope_kind VARCHAR(8),
    scope_id VARCHAR(64),
    config_version VARCHAR(20),
    source_id VARCHAR(64),
    source_epoch VARCHAR(20),
    sequence VARCHAR(20)
) TAGS (point_id VARCHAR(64));

CREATE STABLE IF NOT EXISTS ems_history.telemetry_integer (
    ts TIMESTAMP,
    event_id VARCHAR(64) COMPOSITE KEY,
    sample_value BIGINT,
    quality INT,
    ingest_ts TIMESTAMP,
    time_origin TINYINT,
    time_reason VARCHAR(64),
    quality_reason VARCHAR(64),
    payload_sha256 VARCHAR(64),
    scope_kind VARCHAR(8),
    scope_id VARCHAR(64),
    config_version VARCHAR(20),
    source_id VARCHAR(64),
    source_epoch VARCHAR(20),
    sequence VARCHAR(20)
) TAGS (point_id VARCHAR(64));

CREATE STABLE IF NOT EXISTS ems_history.telemetry_boolean (
    ts TIMESTAMP,
    event_id VARCHAR(64) COMPOSITE KEY,
    sample_value BOOL,
    quality INT,
    ingest_ts TIMESTAMP,
    time_origin TINYINT,
    time_reason VARCHAR(64),
    quality_reason VARCHAR(64),
    payload_sha256 VARCHAR(64),
    scope_kind VARCHAR(8),
    scope_id VARCHAR(64),
    config_version VARCHAR(20),
    source_id VARCHAR(64),
    source_epoch VARCHAR(20),
    sequence VARCHAR(20)
) TAGS (point_id VARCHAR(64));

CREATE STABLE IF NOT EXISTS ems_history.telemetry_text (
    ts TIMESTAMP,
    event_id VARCHAR(64) COMPOSITE KEY,
    sample_value VARCHAR(1024),
    quality INT,
    ingest_ts TIMESTAMP,
    time_origin TINYINT,
    time_reason VARCHAR(64),
    quality_reason VARCHAR(64),
    payload_sha256 VARCHAR(64),
    scope_kind VARCHAR(8),
    scope_id VARCHAR(64),
    config_version VARCHAR(20),
    source_id VARCHAR(64),
    source_epoch VARCHAR(20),
    sequence VARCHAR(20)
) TAGS (point_id VARCHAR(64));

-- MySQL 8: run through the release migration workflow, never during process startup.
-- Keep these rows until both raw and aggregate TAOS retention windows have expired.

CREATE TABLE IF NOT EXISTS history_point_version (
    scope_kind VARCHAR(8) NOT NULL,
    scope_id VARCHAR(64) NOT NULL,
    config_version BIGINT UNSIGNED NOT NULL,
    checksum CHAR(64) NOT NULL,
    point_count INT UNSIGNED NOT NULL,
    ready_at DATETIME(6) NULL,
    PRIMARY KEY (scope_kind, scope_id, config_version)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS history_point_series (
    point_id VARCHAR(64) NOT NULL,
    value_type VARCHAR(16) NOT NULL,
    series_table VARCHAR(96) NOT NULL,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    retired_at DATETIME(6) NULL,
    PRIMARY KEY (point_id),
    UNIQUE KEY uk_history_series_table (series_table),
    CONSTRAINT ck_history_series_type CHECK (value_type IN ('numeric', 'integer', 'boolean', 'text'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS history_point_snapshot (
    scope_kind VARCHAR(8) NOT NULL,
    scope_id VARCHAR(64) NOT NULL,
    config_version BIGINT UNSIGNED NOT NULL,
    point_id VARCHAR(64) NOT NULL,
    asset_id VARCHAR(64) NOT NULL,
    point_code VARCHAR(128) NOT NULL,
    display_name VARCHAR(256) NOT NULL,
    point_kind VARCHAR(16) NOT NULL,
    value_type VARCHAR(16) NOT NULL,
    unit VARCHAR(32) NULL,
    definition_id VARCHAR(64) NULL,
    definition_version BIGINT UNSIGNED NULL,
    enabled BOOLEAN NOT NULL,
    device_path JSON NOT NULL,
    PRIMARY KEY (scope_kind, scope_id, config_version, point_id),
    KEY ix_history_point (point_id, scope_kind, scope_id, config_version),
    CONSTRAINT fk_history_point_version FOREIGN KEY (scope_kind, scope_id, config_version)
        REFERENCES history_point_version (scope_kind, scope_id, config_version),
    CONSTRAINT fk_history_point_series FOREIGN KEY (point_id)
        REFERENCES history_point_series (point_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS history_event_identity (
    event_id VARCHAR(64) NOT NULL,
    point_id VARCHAR(64) NOT NULL,
    source_time_us BIGINT NOT NULL,
    payload_sha256 CHAR(64) NOT NULL,
    first_seen_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (event_id),
    KEY ix_history_identity_point_time (point_id, source_time_us)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS history_quarantine (
    stream_scope VARCHAR(128) NOT NULL,
    stream_id VARCHAR(64) NOT NULL,
    event_id VARCHAR(64) NULL,
    reason VARCHAR(256) NOT NULL,
    raw_payload MEDIUMBLOB NOT NULL,
    status VARCHAR(16) NOT NULL DEFAULT 'open',
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    resolved_at DATETIME(6) NULL,
    PRIMARY KEY (stream_scope, stream_id),
    KEY ix_history_quarantine_status (status, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

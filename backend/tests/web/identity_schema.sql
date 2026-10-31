-- SQLite fixture generated from web_db metadata; attached ems_web mirrors the two-schema runtime.

CREATE TABLE ems_web.audit_log (
	id INTEGER NOT NULL, 
	event_id VARCHAR(64) NOT NULL, 
	actor_user_id VARCHAR(64), 
	target_user_id VARCHAR(64), 
	action VARCHAR(64) NOT NULL, 
	resource_type VARCHAR(32) NOT NULL, 
	resource_id VARCHAR(128), 
	outcome VARCHAR(7) NOT NULL, 
	reason VARCHAR(64), 
	request_id VARCHAR(64) NOT NULL, 
	source_ip VARCHAR(45), 
	details JSON, 
	occurred_at DATETIME NOT NULL, 
	PRIMARY KEY (id), 
	CONSTRAINT uq_audit_log_event_id UNIQUE (event_id), 
	CONSTRAINT ck_audit_log_outcome CHECK (outcome IN ('success', 'failure'))
)

;

CREATE TABLE ems_web.auth_login_policy (
	id INTEGER NOT NULL, 
	failed_login_limit INTEGER DEFAULT 6 NOT NULL, 
	lock_minutes INTEGER DEFAULT 10 NOT NULL, 
	idle_timeout_minutes INTEGER DEFAULT 10 NOT NULL, 
	password_min_length INTEGER DEFAULT 12 NOT NULL, 
	login_notice VARCHAR(500) DEFAULT '' NOT NULL, 
	updated_at DATETIME NOT NULL, 
	PRIMARY KEY (id), 
	CONSTRAINT ck_auth_login_policy_singleton CHECK (id = 1), 
	CONSTRAINT ck_auth_policy_failures CHECK (failed_login_limit BETWEEN 3 AND 6), 
	CONSTRAINT ck_auth_policy_lock CHECK (lock_minutes BETWEEN 10 AND 30), 
	CONSTRAINT ck_auth_policy_idle CHECK (idle_timeout_minutes BETWEEN 10 AND 30), 
	CONSTRAINT ck_auth_policy_password CHECK (password_min_length BETWEEN 12 AND 32), 
	CONSTRAINT ck_auth_policy_notice CHECK (length(login_notice) <= 500)
)

;

CREATE TABLE ems_web.auth_user (
	id INTEGER NOT NULL, 
	user_id VARCHAR(64) NOT NULL, 
	username VARCHAR(64) NOT NULL, 
	display_name VARCHAR(64) NOT NULL, 
	password_hash VARCHAR(255) NOT NULL, 
	role_code VARCHAR(8) DEFAULT 'observer' NOT NULL, 
	status VARCHAR(8) DEFAULT 'disabled' NOT NULL, 
	scope_mode VARCHAR(8) DEFAULT 'selected' NOT NULL, 
	must_change_password INTEGER DEFAULT 1 NOT NULL, 
	temporary_password_expires_at DATETIME, 
	failed_attempts INTEGER DEFAULT 0 NOT NULL, 
	locked_until DATETIME, 
	password_changed_at DATETIME, 
	created_at DATETIME NOT NULL, 
	updated_at DATETIME NOT NULL, 
	last_login_at DATETIME, 
	deleted_at DATETIME, 
	PRIMARY KEY (id), 
	CONSTRAINT uq_auth_user_user_id UNIQUE (user_id), 
	CONSTRAINT uq_auth_user_username UNIQUE (username), 
	CONSTRAINT ck_auth_user_username CHECK (length(username) BETWEEN 4 AND 64 AND username = lower(username)), 
	CONSTRAINT ck_auth_user_name CHECK (length(trim(display_name)) BETWEEN 1 AND 64), 
	CONSTRAINT ck_auth_user_all CHECK (scope_mode <> 'all' OR role_code = 'admin'), 
	CONSTRAINT ck_auth_user_change_flag CHECK (must_change_password IN (0, 1)), 
	CONSTRAINT ck_auth_user_temporary_password CHECK ((must_change_password = 1 AND temporary_password_expires_at IS NOT NULL) OR (must_change_password = 0 AND temporary_password_expires_at IS NULL)), 
	CONSTRAINT ck_auth_user_failures CHECK (failed_attempts BETWEEN 0 AND 6), 
	CONSTRAINT ck_auth_user_deleted CHECK ((status = 'deleted' AND deleted_at IS NOT NULL) OR (status <> 'deleted' AND deleted_at IS NULL)), 
	CONSTRAINT ck_auth_user_role CHECK (role_code IN ('admin', 'operator', 'observer')), 
	CONSTRAINT ck_auth_user_status CHECK (status IN ('enabled', 'disabled', 'deleted')), 
	CONSTRAINT ck_auth_user_scope_mode CHECK (scope_mode IN ('all', 'selected'))
)

;

CREATE TABLE ems_web.auth_session (
	id INTEGER NOT NULL, 
	session_id VARCHAR(64) NOT NULL, 
	user_id VARCHAR(64) NOT NULL, 
	kind VARCHAR(15) NOT NULL, 
	token_hash BLOB NOT NULL, 
	csrf_token CHAR(43) NOT NULL, 
	created_at DATETIME NOT NULL, 
	auth_time DATETIME NOT NULL, 
	last_interactive_at DATETIME NOT NULL, 
	absolute_expires_at DATETIME NOT NULL, 
	revoked_at DATETIME, 
	revoke_reason VARCHAR(32), 
	source_ip VARCHAR(45), 
	user_agent VARCHAR(256) DEFAULT '' NOT NULL, 
	PRIMARY KEY (id), 
	CONSTRAINT uq_auth_session_session_id UNIQUE (session_id), 
	CONSTRAINT uq_auth_session_token_hash UNIQUE (token_hash), 
	CONSTRAINT ck_auth_session_token_hash CHECK (length(token_hash) = 32), 
	CONSTRAINT ck_auth_session_csrf CHECK (length(csrf_token) = 43), 
	CONSTRAINT ck_auth_session_deadline CHECK (absolute_expires_at > created_at), 
	CONSTRAINT ck_auth_session_revocation CHECK ((revoked_at IS NULL AND revoke_reason IS NULL) OR (revoked_at IS NOT NULL AND revoke_reason IS NOT NULL)), 
	FOREIGN KEY(user_id) REFERENCES auth_user (user_id) ON DELETE RESTRICT, 
	CONSTRAINT ck_auth_session_kind CHECK (kind IN ('full', 'password_change'))
)

;

CREATE TABLE ems_web.auth_user_scope (
	id INTEGER NOT NULL, 
	user_id VARCHAR(64) NOT NULL, 
	station_id VARCHAR(64) NOT NULL, 
	device_root_id VARCHAR(64) DEFAULT '' NOT NULL, 
	PRIMARY KEY (id), 
	CONSTRAINT uq_auth_user_scope UNIQUE (user_id, station_id, device_root_id), 
	CONSTRAINT ck_auth_scope_station CHECK (length(station_id) > 0), 
	FOREIGN KEY(user_id) REFERENCES auth_user (user_id) ON DELETE RESTRICT
)

;
INSERT INTO ems_web.auth_login_policy(id,updated_at) VALUES (1,'2026-10-31 00:00:00.000');

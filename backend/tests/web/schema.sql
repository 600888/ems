-- SQLite integration fixture; production schema remains the existing MySQL 003 script.
CREATE TABLE cfg_device (id INTEGER PRIMARY KEY AUTOINCREMENT,device_id TEXT,parent_device_id TEXT,node_kind TEXT,code TEXT,name TEXT,path TEXT,depth INTEGER,sort_order INTEGER,device_type_code TEXT,type_version TEXT,template_id TEXT,source_device_id TEXT,vendor TEXT,model TEXT,firmware_version TEXT,rated_power_kw REAL,timezone TEXT,default_stale_after_ms INTEGER,config_revision INTEGER,enabled INTEGER,description TEXT,legacy_ref TEXT,created_at TEXT,updated_at TEXT,UNIQUE(device_id),UNIQUE(path),UNIQUE(parent_device_id,code));
CREATE TABLE cfg_device_type (id INTEGER PRIMARY KEY AUTOINCREMENT,type_code TEXT,type_version TEXT,name TEXT,category TEXT,status TEXT,description TEXT,created_at TEXT,updated_at TEXT);
CREATE TABLE cfg_type_point (id INTEGER PRIMARY KEY AUTOINCREMENT,type_code TEXT,type_version TEXT,semantic_code TEXT,name TEXT,point_type TEXT,value_type TEXT,unit TEXT,required INTEGER,capability_codes_json TEXT,writable INTEGER,stale_after_ms INTEGER,sort_order INTEGER);
CREATE TABLE cfg_template (id INTEGER PRIMARY KEY AUTOINCREMENT,template_id TEXT,template_version TEXT,device_type_code TEXT,type_version TEXT,vendor TEXT,model TEXT,status TEXT,points_json TEXT,description TEXT,created_at TEXT,updated_at TEXT);
CREATE TABLE cfg_channel (id INTEGER PRIMARY KEY AUTOINCREMENT,channel_id TEXT,code TEXT,name TEXT,protocol TEXT,conn_mode TEXT,endpoint TEXT,serial_json TEXT,poll_interval_ms INTEGER,timeout_ms INTEGER,retry_times INTEGER,max_sessions INTEGER,enabled INTEGER,description TEXT,created_at TEXT,updated_at TEXT,UNIQUE(channel_id),UNIQUE(code));
CREATE TABLE cfg_binding (id INTEGER PRIMARY KEY AUTOINCREMENT,binding_id TEXT,device_id TEXT,channel_id TEXT,device_address TEXT,profile_version TEXT,can_collect INTEGER,can_control INTEGER,can_forward INTEGER,enabled INTEGER,description TEXT,created_at TEXT,updated_at TEXT,UNIQUE(binding_id),UNIQUE(device_id,channel_id));
CREATE TABLE cfg_point_yc (id INTEGER PRIMARY KEY AUTOINCREMENT,point_id TEXT,owner_device_id TEXT,binding_id TEXT,code TEXT,name TEXT,semantic_code TEXT,vendor_key TEXT,origin TEXT,address_space TEXT,address_offset INTEGER,bit_offset INTEGER,register_count INTEGER,protocol_address_json TEXT,wire_type TEXT,byte_order TEXT,value_type TEXT,unit TEXT,decimals INTEGER,scale_factor REAL,offset_value REAL,range_min REAL,range_max REAL,deadband REAL,enabled INTEGER,disabled_reason TEXT,stale_after_ms INTEGER,source_import_row_id TEXT,legacy_ref TEXT,description TEXT,created_at TEXT,updated_at TEXT,UNIQUE(point_id),UNIQUE(owner_device_id,code));
CREATE TABLE cfg_point_yx (id INTEGER PRIMARY KEY AUTOINCREMENT,point_id TEXT,owner_device_id TEXT,binding_id TEXT,code TEXT,name TEXT,semantic_code TEXT,vendor_key TEXT,origin TEXT,address_space TEXT,address_offset INTEGER,bit_offset INTEGER,register_count INTEGER,protocol_address_json TEXT,wire_type TEXT,byte_order TEXT,value_type TEXT,unit TEXT,soe INTEGER,invert INTEGER,enum_json TEXT,unknown_policy TEXT,feedback_of_point_id TEXT,enabled INTEGER,disabled_reason TEXT,stale_after_ms INTEGER,source_import_row_id TEXT,legacy_ref TEXT,description TEXT,created_at TEXT,updated_at TEXT,UNIQUE(point_id),UNIQUE(owner_device_id,code));
CREATE TABLE cfg_point_yk (id INTEGER PRIMARY KEY AUTOINCREMENT,point_id TEXT,owner_device_id TEXT,binding_id TEXT,code TEXT,name TEXT,semantic_code TEXT,vendor_key TEXT,origin TEXT,address_space TEXT,address_offset INTEGER,bit_offset INTEGER,register_count INTEGER,protocol_address_json TEXT,wire_type TEXT,byte_order TEXT,value_type TEXT,unit TEXT,write_mode TEXT,fixed_raw_value INTEGER,pulse_width_ms INTEGER,min_interval_ms INTEGER,write_group_key TEXT,feedback_json TEXT,feedback_timeout_ms INTEGER,failure_policy TEXT,enabled INTEGER,disabled_reason TEXT,stale_after_ms INTEGER,source_import_row_id TEXT,legacy_ref TEXT,description TEXT,created_at TEXT,updated_at TEXT,UNIQUE(point_id),UNIQUE(owner_device_id,code));
CREATE TABLE cfg_point_yt (id INTEGER PRIMARY KEY AUTOINCREMENT,point_id TEXT,owner_device_id TEXT,binding_id TEXT,code TEXT,name TEXT,semantic_code TEXT,vendor_key TEXT,origin TEXT,address_space TEXT,address_offset INTEGER,bit_offset INTEGER,register_count INTEGER,protocol_address_json TEXT,wire_type TEXT,byte_order TEXT,value_type TEXT,unit TEXT,decimals INTEGER,scale_factor REAL,offset_value REAL,range_min REAL,range_max REAL,min_interval_ms INTEGER,write_group_key TEXT,feedback_json TEXT,feedback_timeout_ms INTEGER,failure_policy TEXT,enabled INTEGER,disabled_reason TEXT,stale_after_ms INTEGER,source_import_row_id TEXT,legacy_ref TEXT,description TEXT,created_at TEXT,updated_at TEXT,UNIQUE(point_id),UNIQUE(owner_device_id,code));
CREATE TABLE cfg_import_job (id INTEGER PRIMARY KEY AUTOINCREMENT,import_job_id TEXT,device_id TEXT,binding_id TEXT,source_kind TEXT,file_name TEXT,file_sha256 TEXT,file_bytes INTEGER,address_base TEXT,status TEXT,row_total INTEGER,row_ok INTEGER,row_warning INTEGER,row_error INTEGER,idempotency_key TEXT,submitted_by TEXT,submitted_at TEXT,finished_at TEXT,error_code TEXT,error_message TEXT,UNIQUE(import_job_id),UNIQUE(idempotency_key));
CREATE TABLE cfg_import_row (id INTEGER PRIMARY KEY AUTOINCREMENT,import_job_id TEXT,line_no INTEGER,vendor_key TEXT,vendor_name TEXT,point_type TEXT,target_device_path TEXT,semantic_code TEXT,address_space TEXT,address_offset INTEGER,bit_offset INTEGER,register_count INTEGER,protocol_address_json TEXT,wire_type TEXT,byte_order TEXT,value_type TEXT,scale_factor REAL,offset_value REAL,unit TEXT,parse_status TEXT,parse_code TEXT,parse_message TEXT,confirm_state TEXT,confirm_note TEXT,confirm_by TEXT,confirm_at TEXT,resolved_point_id TEXT,UNIQUE(import_job_id,line_no));
CREATE TABLE ops_comm_stat_5m (id INTEGER PRIMARY KEY AUTOINCREMENT,binding_id TEXT,window_start TEXT,request_total INTEGER,success_total INTEGER,timeout_total INTEGER,error_total INTEGER,avg_latency_ms INTEGER,p95_latency_ms INTEGER,last_valid_at TEXT,last_error_code TEXT);
CREATE TABLE ops_state_log (id INTEGER PRIMARY KEY AUTOINCREMENT,object_kind TEXT,object_id TEXT,state TEXT,since TEXT,reason TEXT);

CREATE TABLE ops_command (id INTEGER PRIMARY KEY AUTOINCREMENT,command_id TEXT COLLATE BINARY NOT NULL,scope_code TEXT NOT NULL,point_id TEXT NOT NULL,value REAL NOT NULL,config_revision INTEGER NOT NULL,expires_at_ms INTEGER NOT NULL,payload_json TEXT NOT NULL,status TEXT NOT NULL,reason TEXT NOT NULL,sent_at_ms INTEGER NOT NULL,updated_at_ms INTEGER NOT NULL,UNIQUE(scope_code,command_id));
CREATE TABLE alarm_archive_progress (
	id INTEGER NOT NULL,
	station_id VARCHAR(64) NOT NULL,
	committed_seq BIGINT NOT NULL,
	archived_seq BIGINT NOT NULL,
	PRIMARY KEY (id),
	CONSTRAINT uq_alarm_archive_progress_station UNIQUE (station_id),
	CONSTRAINT ck_alarm_archive_progress_bounds CHECK (archived_seq >= 0 AND committed_seq >= archived_seq)
);

CREATE TABLE alarm_control (
	id INTEGER NOT NULL,
	alarm_id VARCHAR(64) NOT NULL,
	station_id VARCHAR(64) NOT NULL,
	device_id VARCHAR(64) NOT NULL,
	active_key VARCHAR(128),
	occurred_at_us BIGINT NOT NULL,
	cleared_at_us BIGINT,
	acknowledged_at_us BIGINT,
	state_json TEXT NOT NULL,
	PRIMARY KEY (id),
	CONSTRAINT uq_alarm_control_id UNIQUE (alarm_id),
	CONSTRAINT uq_alarm_control_active_key UNIQUE (station_id, active_key),
	CONSTRAINT ck_alarm_control_active CHECK ((active_key IS NULL AND cleared_at_us IS NOT NULL) OR (active_key IS NOT NULL AND cleared_at_us IS NULL)),
	CONSTRAINT ck_alarm_control_times CHECK (occurred_at_us >= 0 AND (cleared_at_us IS NULL OR cleared_at_us >= occurred_at_us) AND (acknowledged_at_us IS NULL OR acknowledged_at_us >= occurred_at_us))
);
CREATE INDEX ix_alarm_control_active ON alarm_control (station_id, cleared_at_us, alarm_id);

CREATE TABLE alarm_outbox (
	id INTEGER NOT NULL,
	event_id VARCHAR(64) NOT NULL,
	alarm_id VARCHAR(64) NOT NULL,
	station_id VARCHAR(64) NOT NULL,
	device_id VARCHAR(64) NOT NULL,
	stream_seq BIGINT NOT NULL,
	kind INTEGER NOT NULL,
	event_time_us BIGINT NOT NULL,
	event_json TEXT NOT NULL,
	state_json TEXT NOT NULL,
	PRIMARY KEY (id),
	CONSTRAINT uq_alarm_outbox_event UNIQUE (event_id),
	CONSTRAINT uq_alarm_outbox_sequence UNIQUE (station_id, stream_seq),
	CONSTRAINT uq_alarm_outbox_lifecycle UNIQUE (alarm_id, kind),
	CONSTRAINT ck_alarm_outbox_event CHECK (stream_seq > 0 AND event_time_us >= 0 AND kind IN (0, 1, 2))
);

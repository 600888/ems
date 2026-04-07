CREATE TABLE device_group (
	id INTEGER NOT NULL, 
	code VARCHAR(32) NOT NULL, 
	name VARCHAR(64) NOT NULL, 
	parent_id INTEGER, 
	description VARCHAR(256), 
	status INTEGER DEFAULT '0' NOT NULL, 
	enable BOOLEAN DEFAULT '1' NOT NULL, 
	created_at DATETIME DEFAULT CURRENT_TIMESTAMP NOT NULL, 
	updated_at DATETIME DEFAULT CURRENT_TIMESTAMP NOT NULL, 
	PRIMARY KEY (id), 
	FOREIGN KEY(parent_id) REFERENCES device_group (id)
);
CREATE UNIQUE INDEX ix_device_group_code ON device_group (code);
CREATE INDEX ix_device_group_parent_id ON device_group (parent_id);
CREATE TABLE device (
	id INTEGER NOT NULL, 
	code VARCHAR(32) NOT NULL, 
	name VARCHAR(64) NOT NULL, 
	device_type INTEGER DEFAULT '0' NOT NULL, 
	group_id INTEGER, 
	enable BOOLEAN DEFAULT '1' NOT NULL, 
	created_at DATETIME DEFAULT CURRENT_TIMESTAMP NOT NULL, 
	updated_at DATETIME DEFAULT CURRENT_TIMESTAMP NOT NULL, 
	PRIMARY KEY (id), 
	FOREIGN KEY(group_id) REFERENCES device_group (id)
);
CREATE UNIQUE INDEX ix_device_code ON device (code);
CREATE INDEX ix_device_group_id ON device (group_id);
CREATE TABLE channel (
	id INTEGER NOT NULL, 
	code VARCHAR(32) NOT NULL, 
	name VARCHAR(64) NOT NULL, 
	device_id INTEGER, 
	protocol_type INTEGER DEFAULT '1' NOT NULL, 
	conn_type INTEGER DEFAULT '1' NOT NULL, 
	ip VARCHAR(32), 
	port INTEGER, 
	com_port VARCHAR(16), 
	baud_rate INTEGER DEFAULT '9600', 
	data_bits INTEGER DEFAULT '8', 
	stop_bits INTEGER DEFAULT '1', 
	parity VARCHAR(1) DEFAULT 'N', 
	rtu_addr VARCHAR(16) DEFAULT '1' NOT NULL, 
	timeout INTEGER DEFAULT '5' NOT NULL, 
	enable BOOLEAN DEFAULT '1' NOT NULL, 
	PRIMARY KEY (id), 
	FOREIGN KEY(device_id) REFERENCES device (id)
);
CREATE UNIQUE INDEX ix_channel_code ON channel (code);
CREATE TABLE point_yc (
	id INTEGER NOT NULL, 
	code VARCHAR(64) NOT NULL, 
	name VARCHAR(64) NOT NULL, 
	channel_id INTEGER, 
	rtu_addr INTEGER DEFAULT '1' NOT NULL, 
	reg_addr VARCHAR(32) NOT NULL, 
	func_code INTEGER DEFAULT '3' NOT NULL, 
	decode_code VARCHAR(10) DEFAULT '0x41' NOT NULL, 
	mul_coe FLOAT DEFAULT '1.0' NOT NULL, 
	add_coe FLOAT DEFAULT '0.0' NOT NULL, 
	max_limit FLOAT DEFAULT '9999999' NOT NULL, 
	min_limit FLOAT DEFAULT '-9999999' NOT NULL, 
	iec_common_address INTEGER, 
	iec_cot INTEGER DEFAULT '3', 
	iec_quality INTEGER DEFAULT '0', 
	enable BOOLEAN DEFAULT '1' NOT NULL, 
	PRIMARY KEY (id), 
	CONSTRAINT uq_point_yc_code_channel_rtu UNIQUE (code, channel_id, rtu_addr), 
	FOREIGN KEY(channel_id) REFERENCES channel (id)
);
CREATE INDEX ix_point_yc_code ON point_yc (code);
CREATE INDEX ix_point_yc_channel_id ON point_yc (channel_id);
CREATE TABLE point_yx (
	id INTEGER NOT NULL, 
	code VARCHAR(64) NOT NULL, 
	name VARCHAR(64) NOT NULL, 
	channel_id INTEGER, 
	rtu_addr INTEGER DEFAULT '1' NOT NULL, 
	reg_addr VARCHAR(32) NOT NULL, 
	func_code INTEGER DEFAULT '1' NOT NULL, 
	decode_code VARCHAR(10) DEFAULT '0x20' NOT NULL, 
	bit INTEGER, 
	reverse BOOLEAN DEFAULT '0' NOT NULL, 
	iec_common_address INTEGER, 
	iec_cot INTEGER DEFAULT '3', 
	iec_quality INTEGER DEFAULT '0', 
	enable BOOLEAN DEFAULT '1' NOT NULL, 
	PRIMARY KEY (id), 
	CONSTRAINT uq_point_yx_code_channel_rtu UNIQUE (code, channel_id, rtu_addr), 
	FOREIGN KEY(channel_id) REFERENCES channel (id)
);
CREATE INDEX ix_point_yx_channel_id ON point_yx (channel_id);
CREATE INDEX ix_point_yx_code ON point_yx (code);
CREATE TABLE point_yk (
	id INTEGER NOT NULL, 
	code VARCHAR(64) NOT NULL, 
	name VARCHAR(64) NOT NULL, 
	channel_id INTEGER, 
	rtu_addr INTEGER DEFAULT '1' NOT NULL, 
	reg_addr VARCHAR(32) NOT NULL, 
	func_code INTEGER DEFAULT '5' NOT NULL, 
	decode_code VARCHAR(10) DEFAULT '0x20' NOT NULL, 
	bit INTEGER, 
	command_type INTEGER DEFAULT '0' NOT NULL, 
	related_yx_id INTEGER, 
	iec_common_address INTEGER, 
	iec_cot INTEGER DEFAULT '3', 
	iec_quality INTEGER DEFAULT '0', 
	enable BOOLEAN DEFAULT '1' NOT NULL, 
	PRIMARY KEY (id), 
	CONSTRAINT uq_point_yk_code_channel_rtu UNIQUE (code, channel_id, rtu_addr), 
	FOREIGN KEY(channel_id) REFERENCES channel (id), 
	FOREIGN KEY(related_yx_id) REFERENCES point_yx (id)
);
CREATE INDEX ix_point_yk_code ON point_yk (code);
CREATE INDEX ix_point_yk_channel_id ON point_yk (channel_id);
CREATE TABLE point_yt (
	id INTEGER NOT NULL, 
	code VARCHAR(64) NOT NULL, 
	name VARCHAR(64) NOT NULL, 
	channel_id INTEGER, 
	rtu_addr INTEGER DEFAULT '1' NOT NULL, 
	reg_addr VARCHAR(32) NOT NULL, 
	func_code INTEGER DEFAULT '3' NOT NULL, 
	decode_code VARCHAR(10) DEFAULT '0x20' NOT NULL, 
	mul_coe FLOAT DEFAULT '1.0' NOT NULL, 
	add_coe FLOAT DEFAULT '0.0' NOT NULL, 
	max_limit FLOAT DEFAULT '9999999' NOT NULL, 
	min_limit FLOAT DEFAULT '-9999999' NOT NULL, 
	related_yc_id INTEGER, 
	iec_common_address INTEGER, 
	iec_cot INTEGER DEFAULT '3', 
	iec_quality INTEGER DEFAULT '0', 
	enable BOOLEAN DEFAULT '1' NOT NULL, 
	PRIMARY KEY (id), 
	CONSTRAINT uq_point_yt_code_channel_rtu UNIQUE (code, channel_id, rtu_addr), 
	FOREIGN KEY(channel_id) REFERENCES channel (id), 
	FOREIGN KEY(related_yc_id) REFERENCES point_yc (id)
);
CREATE INDEX ix_point_yt_channel_id ON point_yt (channel_id);
CREATE INDEX ix_point_yt_code ON point_yt (code);
CREATE TABLE slave (
	id INTEGER NOT NULL, 
	channel_id INTEGER NOT NULL, 
	slave_id INTEGER NOT NULL, 
	name VARCHAR(64), 
	enable BOOLEAN DEFAULT '1' NOT NULL, 
	PRIMARY KEY (id), 
	CONSTRAINT uq_slave_channel_slave UNIQUE (channel_id, slave_id), 
	FOREIGN KEY(channel_id) REFERENCES channel (id)
);
CREATE INDEX ix_slave_channel_id ON slave (channel_id);
CREATE TABLE point_mapping (
	id INTEGER NOT NULL, 
	device_name VARCHAR(64) NOT NULL, 
	target_point_code VARCHAR(64) NOT NULL, 
	source_point_codes TEXT NOT NULL, 
	formula TEXT NOT NULL, 
	enable BOOLEAN DEFAULT '1' NOT NULL, 
	PRIMARY KEY (id), 
	CONSTRAINT uix_device_target_point UNIQUE (device_name, target_point_code)
);

-- Optional development demo; apply explicitly after 000_configuration_mysql.sql.

INSERT IGNORE INTO device (id, code, name, device_type, enable)
VALUES (1, 'PCS_001', 'PCS-1 储能变流器', 11, 1);
INSERT IGNORE INTO channel (id, code, name, device_id, protocol_type, conn_type,
    ip, port, rtu_addr, timeout, enable)
VALUES (1, 'CH_MODBUS_001', 'Modbus-TCP-1', 1, 2, 2, '127.0.0.1', 502, '1', 5, 1);
INSERT IGNORE INTO point_yc (id, code, name, channel_id, device_id, protocol_type, slave_id,
    func_code, reg_addr, decode_code, endian, unit, enable) VALUES
(1,'totalActivePower','总有功功率',1,1,2,1,3,0,4,0,'kW',1),
(2,'totalReactivePower','总无功功率',1,1,2,1,3,2,4,0,'kVar',1),
(3,'phaseAVoltage','A相电压',1,1,2,1,3,4,4,0,'V',1),
(4,'phaseBVoltage','B相电压',1,1,2,1,3,6,4,0,'V',1),
(5,'phaseCVoltage','C相电压',1,1,2,1,3,8,4,0,'V',1),
(6,'phaseACurrent','A相电流',1,1,2,1,3,10,4,0,'A',1),
(7,'phaseBCurrent','B相电流',1,1,2,1,3,12,4,0,'A',1),
(8,'phaseCCurrent','C相电流',1,1,2,1,3,14,4,0,'A',1),
(9,'gridFrequency','电网频率',1,1,2,1,3,16,4,0,'Hz',1),
(10,'soc','SOC',1,1,2,1,3,18,4,0,'%',1);
INSERT IGNORE INTO point_yx (id, code, name, channel_id, device_id, protocol_type, slave_id,
    func_code, reg_addr, decode_code, endian, enable) VALUES
(1,'runningStatus','运行状态',1,1,2,1,1,0,8,0,1),
(2,'faultAlarm','故障告警',1,1,2,1,1,0,8,0,1),
(3,'commStatus','通信状态',1,1,2,1,1,0,8,0,1);

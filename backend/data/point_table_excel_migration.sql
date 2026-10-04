-- MySQL 四遥 Excel 导入迁移；连接到 EMS 配置库后执行，可重复运行。
-- 不修改点位数值；历史重复编码须先处理后再创建唯一索引。

SET @ems_point_ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='point_yc' AND column_name='func_code') = 0, 'ALTER TABLE point_yc ADD COLUMN func_code INT DEFAULT 3', 'SELECT 1');
PREPARE ems_point_stmt FROM @ems_point_ddl;
EXECUTE ems_point_stmt;
DEALLOCATE PREPARE ems_point_stmt;

SET @ems_point_ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='point_yc' AND column_name='endian') = 0, 'ALTER TABLE point_yc ADD COLUMN endian INT DEFAULT 0', 'SELECT 1');
PREPARE ems_point_stmt FROM @ems_point_ddl;
EXECUTE ems_point_stmt;
DEALLOCATE PREPARE ems_point_stmt;

SET @ems_point_ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='point_yc' AND column_name='iec104_type') = 0, 'ALTER TABLE point_yc ADD COLUMN iec104_type VARCHAR(32) NOT NULL DEFAULT ''''', 'SELECT 1');
PREPARE ems_point_stmt FROM @ems_point_ddl;
EXECUTE ems_point_stmt;
DEALLOCATE PREPARE ems_point_stmt;

SET @ems_point_ddl = IF((SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='point_yc' AND index_name='uq_point_yc_code') = 0, 'ALTER TABLE point_yc ADD UNIQUE KEY uq_point_yc_code (code)', 'SELECT 1');
PREPARE ems_point_stmt FROM @ems_point_ddl;
EXECUTE ems_point_stmt;
DEALLOCATE PREPARE ems_point_stmt;

SET @ems_point_ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='point_yx' AND column_name='func_code') = 0, 'ALTER TABLE point_yx ADD COLUMN func_code INT DEFAULT 1', 'SELECT 1');
PREPARE ems_point_stmt FROM @ems_point_ddl;
EXECUTE ems_point_stmt;
DEALLOCATE PREPARE ems_point_stmt;

SET @ems_point_ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='point_yx' AND column_name='endian') = 0, 'ALTER TABLE point_yx ADD COLUMN endian INT DEFAULT 0', 'SELECT 1');
PREPARE ems_point_stmt FROM @ems_point_ddl;
EXECUTE ems_point_stmt;
DEALLOCATE PREPARE ems_point_stmt;

SET @ems_point_ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='point_yx' AND column_name='iec104_type') = 0, 'ALTER TABLE point_yx ADD COLUMN iec104_type VARCHAR(32) NOT NULL DEFAULT ''''', 'SELECT 1');
PREPARE ems_point_stmt FROM @ems_point_ddl;
EXECUTE ems_point_stmt;
DEALLOCATE PREPARE ems_point_stmt;

SET @ems_point_ddl = IF((SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='point_yx' AND index_name='uq_point_yx_code') = 0, 'ALTER TABLE point_yx ADD UNIQUE KEY uq_point_yx_code (code)', 'SELECT 1');
PREPARE ems_point_stmt FROM @ems_point_ddl;
EXECUTE ems_point_stmt;
DEALLOCATE PREPARE ems_point_stmt;

SET @ems_point_ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='point_yk' AND column_name='func_code') = 0, 'ALTER TABLE point_yk ADD COLUMN func_code INT DEFAULT 5', 'SELECT 1');
PREPARE ems_point_stmt FROM @ems_point_ddl;
EXECUTE ems_point_stmt;
DEALLOCATE PREPARE ems_point_stmt;

SET @ems_point_ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='point_yk' AND column_name='endian') = 0, 'ALTER TABLE point_yk ADD COLUMN endian INT DEFAULT 0', 'SELECT 1');
PREPARE ems_point_stmt FROM @ems_point_ddl;
EXECUTE ems_point_stmt;
DEALLOCATE PREPARE ems_point_stmt;

SET @ems_point_ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='point_yk' AND column_name='iec104_type') = 0, 'ALTER TABLE point_yk ADD COLUMN iec104_type VARCHAR(32) NOT NULL DEFAULT ''''', 'SELECT 1');
PREPARE ems_point_stmt FROM @ems_point_ddl;
EXECUTE ems_point_stmt;
DEALLOCATE PREPARE ems_point_stmt;

SET @ems_point_ddl = IF((SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='point_yk' AND index_name='uq_point_yk_code') = 0, 'ALTER TABLE point_yk ADD UNIQUE KEY uq_point_yk_code (code)', 'SELECT 1');
PREPARE ems_point_stmt FROM @ems_point_ddl;
EXECUTE ems_point_stmt;
DEALLOCATE PREPARE ems_point_stmt;

SET @ems_point_ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='point_yt' AND column_name='func_code') = 0, 'ALTER TABLE point_yt ADD COLUMN func_code INT DEFAULT 6', 'SELECT 1');
PREPARE ems_point_stmt FROM @ems_point_ddl;
EXECUTE ems_point_stmt;
DEALLOCATE PREPARE ems_point_stmt;

SET @ems_point_ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='point_yt' AND column_name='endian') = 0, 'ALTER TABLE point_yt ADD COLUMN endian INT DEFAULT 0', 'SELECT 1');
PREPARE ems_point_stmt FROM @ems_point_ddl;
EXECUTE ems_point_stmt;
DEALLOCATE PREPARE ems_point_stmt;

SET @ems_point_ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='point_yt' AND column_name='iec104_type') = 0, 'ALTER TABLE point_yt ADD COLUMN iec104_type VARCHAR(32) NOT NULL DEFAULT ''''', 'SELECT 1');
PREPARE ems_point_stmt FROM @ems_point_ddl;
EXECUTE ems_point_stmt;
DEALLOCATE PREPARE ems_point_stmt;

SET @ems_point_ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='point_yt' AND column_name='related_yc_id') = 0, 'ALTER TABLE point_yt ADD COLUMN related_yc_id BIGINT NOT NULL DEFAULT 0', 'SELECT 1');
PREPARE ems_point_stmt FROM @ems_point_ddl;
EXECUTE ems_point_stmt;
DEALLOCATE PREPARE ems_point_stmt;

SET @ems_point_ddl = IF((SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='point_yt' AND index_name='uq_point_yt_code') = 0, 'ALTER TABLE point_yt ADD UNIQUE KEY uq_point_yt_code (code)', 'SELECT 1');
PREPARE ems_point_stmt FROM @ems_point_ddl;
EXECUTE ems_point_stmt;
DEALLOCATE PREPARE ems_point_stmt;

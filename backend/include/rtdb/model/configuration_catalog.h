#pragma once

namespace ems::database {
struct DeviceConfigRecords;
}

#include <rtdb/model/runtime_catalog.h>

#include <memory>

namespace ems::rtdb {
/**
 * @brief 将当前设备树和四遥配置编译为统一站级实时目录，不访问数据库。
 * @param[in] records 同一配置事务读取的设备、通道和四遥行。
 * @return 完整只读目录；无站根、非法路径或类型返回错误，不发布半成品。
 */
EMS_RTDB_API Result<std::shared_ptr<const RuntimeCatalog>> configuration_catalog(
    const database::DeviceConfigRecords& records);
}  // namespace ems::rtdb

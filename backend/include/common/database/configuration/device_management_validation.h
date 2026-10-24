#pragma once
#include <common/database/configuration/i_device_config_repository.h>

namespace ems::database {
struct DeviceFinding {
    std::string reason;
    std::string device_id;
    std::string point_id;
};

/**
 * @brief 校验当前候选工程的树、引用、四遥、能力完整性和物理地址占用。
 * @param[in] records 同事务读取的完整工程。
 * @param[in] confirmed_import_points 已核对当前地址与导入确认一致的稳定点 ID。
 * @return 阻断性问题集合；不访问网络或数据库。
 */
std::vector<DeviceFinding> validate_device_management(
    const DeviceConfigRecords& records,
    const std::vector<std::string>& confirmed_import_points = {});
}  // namespace ems::database

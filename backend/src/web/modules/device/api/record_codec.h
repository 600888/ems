#pragma once
#include <common/database/configuration/i_device_management_store.h>

#include <nlohmann/json.hpp>

namespace ems::web {
using Json = nlohmann::json;
/**
 * @brief 将公共行编码为 JSON，数字内部主键保留以供服务使用，对外须经 DTO 清理。
 * @param[in] record 公共行。
 * @return JSON 对象。
 */
Json encode_record(const database::DeviceManagementRecord& record);
/**
 * @brief 按固定表类型解码行；调用方先完成可写字段白名单检查。
 * @param[in] table 固定表枚举。
 * @param[in] json 行字段对象。
 * @return 公共行；非法类型抛 JSON 异常。
 */
database::DeviceManagementRecord decode_record(database::DeviceTable table, const Json& json);
}  // namespace ems::web

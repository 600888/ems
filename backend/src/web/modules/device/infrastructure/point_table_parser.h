#pragma once
#include "web/modules/device/application/device_service.h"

namespace ems::web {
/**
 * @brief 在数据库事务之外受限解析原文件。
 * @param[in] metadata 明确格式与目标。
 * @param[in] upload 原始文件。
 * @param[in] workspace 服务端受限临时目录。
 * @return 规范行和诊断；不修改工程表。
 */
Json parse_upload(const Json& metadata, const Upload& upload, const std::string& workspace);
}  // namespace ems::web

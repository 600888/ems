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

/**
 * @brief 解析 vendor_xlsx 点表字节为导入预览行（单设备导入与整站包共用）。
 *
 * 在交给 Excel 库之前核对 ZIP 目录的总解压预算，阻止压缩炸弹；
 * 列白名单与单文件导入一致。写临时文件后调用公共解析器。
 * @param[in] bytes xlsx 原始字节。
 * @param[in] workspace 受限工作区（临时文件目录）。
 * @return {rows, issues} 预览结果。
 */
Json parse_vendor_xlsx(const std::string& bytes, const std::string& workspace);
}  // namespace ems::web

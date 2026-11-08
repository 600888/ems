#pragma once
#include "web/modules/device/application/device_service.h"

namespace ems::web {
/**
 * @brief 在数据库事务之外受限解析原文件。
 * @param[in] metadata 明确格式与目标。
 * @param[in] upload 原始文件。
 * @param[in] workspace 服务端受限临时目录。
 * @param[in] protocol 目标绑定的通道协议，用于解释四遥模板中的协议地址。
 * @return 规范行和诊断；不修改工程表。
 */
Json parse_upload(const Json& metadata, const Upload& upload, const std::string& workspace,
                  const std::string& protocol = "modbus");

/**
 * @brief 识别英文点表或中文四遥 XLSX，生成单设备及整站共用的导入预览。
 *
 * 在交给 Excel 库之前核对 ZIP 目录的总解压预算，阻止压缩炸弹；
 * 英文列复用公共寄存器解析器；中文四遥保留分类和协议专用地址。
 * @param[in] bytes xlsx 原始字节。
 * @param[in] workspace 受限工作区（临时文件目录）。
 * @param[in] protocol 目标通道协议，四遥模板按此协议解释。
 * @return {rows, issues} 预览结果。
 */
Json parse_vendor_xlsx(const std::string& bytes, const std::string& workspace,
                       const std::string& protocol = "modbus");
}  // namespace ems::web

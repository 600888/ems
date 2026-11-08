#pragma once

#include <util/excel.hpp>

#include "web/transport/json.h"

namespace ems::web {
/**
 * @brief 解析 data/point_csv 四遥工作簿，保留协议地址、倍率和反馈关联。
 * @param[in] excel 已打开的只读工作簿，所有权仍归调用方。
 * @param[in] protocol 目标通道协议：modbus、iec104 或 dlt645。
 * @return 规范行及包含工作表、行号、字段的诊断；不写配置，不执行命令。
 */
Json parse_telemetry_xlsx(const util::Excel& excel, const std::string& protocol);
}  // namespace ems::web

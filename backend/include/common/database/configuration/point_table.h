#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "point.h"

namespace ems::database {

/// 追加拒绝已有编码；替换仅重建选定设备及通道的四遥配置。
enum class PointTableImportMode { Append, Replace };

/// Excel 错误位置；line 为零时表示文件或数据库错误。
struct PointTableError {
    std::string sheet;
    std::size_t line{0};
    std::string column;
    std::string reason;
};

/// 四遥配置草稿，关联编码在入库时解析为数据库 ID。
struct PointTableData {
    std::vector<AnalogPoint> yc;
    std::vector<StatusPoint> yx;
    std::vector<ControlPoint> yk;
    std::vector<SetpointPoint> yt;
    std::unordered_map<std::string, std::string> yk_feedback;
    std::unordered_map<std::string, std::string> yt_feedback;
};

/// 导入结果；解析成功并不代表已入库，committed 为 true 才表示事务已提交。
struct PointTableImportResult {
    PointTableData data;
    std::vector<PointTableError> errors;
    std::size_t inserted{0};
    bool committed{false};

    /**
     * @brief 判断文件解析或数据库操作是否没有错误。
     * @return true 表示没有错误；入库状态需另检查 committed。
     */
    bool ok() const noexcept { return errors.empty(); }
};

/**
 * @brief 解析四遥 XLSX 模板，不修改数据库、不计算公式、不发送设备命令。
 * @param[in] path UTF-8 XLSX 文件路径；不支持旧版二进制 XLS。
 * @param[in] protocol_type 选定通道协议：0=DLT645、1=Modbus RTU、2=Modbus TCP、3=IEC104。
 * @param[in] channel_id 选定的数据库通道 ID，须为正数。
 * @param[in] device_id 选定的数据库设备 ID，须为正数。
 * @return 四遥配置及工作表/行/列错误；存在任何错误时禁止入库。
 */
PointTableImportResult parse_point_table_excel(const std::string& path, int protocol_type,
                                               int64_t channel_id, int64_t device_id);

}  // namespace ems::database

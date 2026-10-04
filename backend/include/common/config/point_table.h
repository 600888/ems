/**
 * @file point_table.h
 * @brief 设备点表 CSV/XLSX 导入与内存配置加载。
 */
#pragma once

#include <common/model/device_config.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ems::config {

/// 点表文件中的一个厂家点；空 standard_code 表示仅保留原始点定义。
struct PointTableRow {
    std::size_t line{0};
    std::string template_key;
    std::string standard_code;
    std::string vendor_name;
    std::string address_space;
    std::uint32_t address_offset{0};
    std::string access;
    std::string wire_type;
    std::string byte_order;
    double scale{1.0};
    double offset{0.0};
    std::string unit;
};

/// 导入或加载错误，line 为零时表示文件或整体配置错误。
struct PointTableIssue {
    std::size_t line{0};
    std::string column;
    std::string reason;
};

/// CSV/XLSX 解析结果；解析只生成草稿，不修改配置或发起设备操作。
struct PointTableImportResult {
    core::DeviceId device_id;
    std::string binding_id;
    std::vector<PointTableRow> rows;
    std::vector<PointTableIssue> issues;

    /**
     * @brief 判断导入文件是否通过结构校验。
     * @return true 表示没有结构错误。
     */
    bool ok() const noexcept { return issues.empty(); }
};

/// 加载策略：追加只接受尚未绑定的标准点；替换重建本设备的标准投影路由。
enum class PointTableLoadMode { Append, Replace };

/// 原子加载结果；失败时 catalog 和 draft 保持输入内容。
struct PointTableLoadResult {
    model::ModelCatalog catalog;
    model::DevicePointDraft draft;
    std::vector<PointTableRow> vendor_rows;
    std::vector<PointTableIssue> issues;
    bool applied{false};

    /**
     * @brief 判断候选配置是否完整加载成功。
     * @return true 表示已应用且没有错误。
     */
    bool ok() const noexcept { return applied && issues.empty(); }
};

/**
 * @brief 解析新格式 UTF-8 CSV 点表，保留行号并聚合字段错误。
 * @param[in] csv_text CSV 原始文本，最大 2 MiB。
 * @param[in] device_id 已选定的实际设备 ID，不从文件猜测。
 * @param[in] binding_id 已选定的设备通道绑定 ID，不从文件猜测。
 * @return 解析后的厂家点草稿或逐行错误；无配置写入。
 */
PointTableImportResult parse_point_table_csv(const std::string& csv_text,
                                             const core::DeviceId& device_id,
                                             const std::string& binding_id);

/**
 * @brief 读取新格式 CSV 或 XLSX 点表；XLSX 沿用 CSV 列名，不计算公式。
 * @param[in] path UTF-8 文件路径；XLSX 使用单工作表或名为“点表”的工作表，不支持 XLS。
 * @param[in] device_id 已选定的实际设备 ID。
 * @param[in] binding_id 已选定的设备通道绑定 ID。
 * @return 解析后的厂家点草稿或文件、逐行错误；无配置写入。
 * @note 四遥中文模板及数据库写入使用 db::DbManager::import_point_table。
 */
PointTableImportResult import_point_table_file(const std::string& path,
                                               const core::DeviceId& device_id,
                                               const std::string& binding_id);

/**
 * @brief 将已解析点表原子加载到候选设备目录和固有点草稿。
 * @param[in] catalog 已建立索引的现有目录，含稳定的设备、点和通道绑定。
 * @param[in] draft 对应设备的现有固有点草稿。
 * @param[in] imported 已通过解析的点表草稿。
 * @param[in] mode 追加或替换本设备标准投影的模式。
 * @return 新候选配置；失败时返回原目录和草稿及具体错误。
 * @note 结果仍为草稿；命令编码、反馈、持久化发布需另行校验。
 */
PointTableLoadResult load_point_table(const model::ModelCatalog& catalog,
                                      const model::DevicePointDraft& draft,
                                      const PointTableImportResult& imported,
                                      PointTableLoadMode mode);

}  // namespace ems::config

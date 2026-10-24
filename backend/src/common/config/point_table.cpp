/**
 * @file point_table.cpp
 * @brief 明确列格式的 Modbus CSV 点表解析与原子内存加载。
 */
#include <common/config/point_table.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "util/excel.hpp"

namespace ems::config {
namespace {

constexpr std::size_t kMaxFileBytes = 2 * 1024 * 1024;
constexpr std::size_t kMaxRows = 10000;
constexpr std::size_t kMaxIssues = 256;

struct CsvRecord {
    std::size_t line{0};
    std::vector<std::string> cells;
};

void issue(std::vector<PointTableIssue>& issues, std::size_t line, std::string column,
           std::string reason) {
    if (issues.size() < kMaxIssues) {
        issues.push_back({line, std::move(column), std::move(reason)});
    }
}

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool valid_utf8(const std::string& text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i]);
        std::size_t width = 0;
        if (first < 0x80) {
            width = 1;
        } else if (first >= 0xC2 && first <= 0xDF) {
            width = 2;
        } else if (first >= 0xE0 && first <= 0xEF) {
            width = 3;
        } else if (first >= 0xF0 && first <= 0xF4) {
            width = 4;
        } else {
            return false;
        }
        if (i + width > text.size()) return false;
        for (std::size_t j = 1; j < width; ++j) {
            if ((static_cast<unsigned char>(text[i + j]) & 0xC0) != 0x80) return false;
        }
        if (width >= 3) {
            const auto second = static_cast<unsigned char>(text[i + 1]);
            if ((first == 0xE0 && second < 0xA0) || (first == 0xED && second >= 0xA0) ||
                (first == 0xF0 && second < 0x90) || (first == 0xF4 && second >= 0x90)) {
                return false;
            }
        }
        i += width;
    }
    return true;
}

std::vector<CsvRecord> parse_csv_records(const std::string& text,
                                         std::vector<PointTableIssue>& issues) {
    std::vector<CsvRecord> records;
    CsvRecord record;
    record.line = 1;
    std::string cell;
    bool quoted = false;
    bool after_quote = false;
    std::size_t line = 1;
    auto finish_record = [&]() {
        record.cells.push_back(trim(cell));
        cell.clear();
        bool nonempty = false;
        for (const auto& value : record.cells) nonempty |= !value.empty();
        if (nonempty) records.push_back(std::move(record));
        record = CsvRecord{};
        record.line = line + 1;
        after_quote = false;
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    cell.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                    after_quote = true;
                }
            } else {
                cell.push_back(c);
                if (c == '\n') ++line;
            }
            continue;
        }
        if (c == '"') {
            if (!trim(cell).empty() || after_quote) {
                issue(issues, line, "csv", "引号只能出现在字段开头");
            } else {
                cell.clear();
                quoted = true;
            }
        } else if (c == ',') {
            record.cells.push_back(trim(cell));
            cell.clear();
            after_quote = false;
        } else if (c == '\n') {
            finish_record();
            ++line;
            record.line = line;
        } else if (c == '\r') {
            if (i + 1 >= text.size() || text[i + 1] != '\n') {
                finish_record();
                ++line;
                record.line = line;
            }
        } else if (after_quote && c != ' ' && c != '\t') {
            issue(issues, line, "csv", "闭合引号后只能出现分隔符");
        } else if (!after_quote) {
            cell.push_back(c);
        }
        if (records.size() > kMaxRows + 1) {
            issue(issues, line, "csv", "点表行数超过 10000");
            return records;
        }
    }
    if (quoted) issue(issues, record.line, "csv", "引号未闭合");
    if (!record.cells.empty() || !cell.empty()) finish_record();
    return records;
}

bool parse_uint16(const std::string& text, std::uint32_t& value) {
    if (text.empty() || text.front() == '-') return false;
    try {
        std::size_t consumed = 0;
        const unsigned long parsed = std::stoul(text, &consumed, 10);
        if (consumed != text.size() || parsed > 65535) return false;
        value = static_cast<std::uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_finite(const std::string& text, double& value) {
    try {
        std::size_t consumed = 0;
        const double parsed = std::stod(text, &consumed);
        if (consumed != text.size() || !std::isfinite(parsed)) return false;
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool valid_space(const PointTableRow& row) {
    if (row.address_space == "coil") return row.wire_type == "bool";
    if (row.address_space == "discrete_input") {
        return row.access == "read" && row.wire_type == "bool";
    }
    if (row.address_space == "input_register") {
        return row.access == "read" && row.wire_type != "bool";
    }
    return row.address_space == "holding_register" && row.wire_type != "bool";
}

bool valid_wire_type(const std::string& type) {
    return type == "bool" || type == "i16" || type == "u16" || type == "i32" || type == "u32" ||
           type == "f32";
}

bool has_op(const model::DeviceChannelBinding& binding, model::BindingOp operation) {
    return std::find(binding.operations.begin(), binding.operations.end(), operation) !=
           binding.operations.end();
}

}  // namespace

PointTableImportResult parse_point_table_csv(const std::string& csv_text,
                                             const core::DeviceId& device_id,
                                             const std::string& binding_id) {
    PointTableImportResult result;
    result.device_id = device_id;
    result.binding_id = binding_id;
    if (device_id.empty()) issue(result.issues, 0, "device_id", "必须先选择实际设备");
    if (binding_id.empty()) issue(result.issues, 0, "binding_id", "必须先选择设备通道绑定");
    if (csv_text.size() > kMaxFileBytes) {
        issue(result.issues, 0, "file", "点表文件超过 2 MiB");
        return result;
    }
    std::string text = csv_text;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    if (!valid_utf8(text)) {
        issue(result.issues, 0, "file", "文件不是有效的 UTF-8 文本");
        return result;
    }
    auto records = parse_csv_records(text, result.issues);
    if (records.size() > kMaxRows + 1) {
        issue(result.issues, 0, "file", "点表行数超过 10000");
        return result;
    }
    if (records.empty()) {
        issue(result.issues, 0, "header", "点表为空");
        return result;
    }

    const std::vector<std::string> required = {
        "template_key",   "standard_code", "vendor_name", "address_space",
        "address_offset", "access",        "wire_type",   "byte_order",
        "scale",          "offset",        "unit"};
    std::unordered_map<std::string, std::size_t> columns;
    for (std::size_t i = 0; i < records.front().cells.size(); ++i) {
        const auto& name = records.front().cells[i];
        if (!columns.emplace(name, i).second) {
            issue(result.issues, records.front().line, name, "列名重复");
        }
    }
    for (const auto& name : required) {
        if (columns.count(name) == 0) {
            issue(result.issues, records.front().line, name, "缺少必填列");
        }
    }
    if (!result.issues.empty()) return result;

    std::unordered_set<std::string> keys;
    std::unordered_set<std::string> standard_codes;
    for (std::size_t i = 1; i < records.size(); ++i) {
        const auto& record = records[i];
        if (record.cells.size() != records.front().cells.size()) {
            issue(result.issues, record.line, "csv", "字段数量与表头不一致");
            continue;
        }
        auto get = [&](const char* name) -> const std::string& {
            return record.cells[columns.at(name)];
        };
        PointTableRow row;
        row.line = record.line;
        row.template_key = get("template_key");
        row.standard_code = get("standard_code");
        row.vendor_name = get("vendor_name");
        row.address_space = get("address_space");
        row.access = get("access");
        row.wire_type = get("wire_type");
        row.byte_order = get("byte_order");
        row.unit = get("unit");
        if (row.template_key.empty()) {
            issue(result.issues, row.line, "template_key", "厂家点稳定键不能为空");
        } else if (!keys.insert(row.template_key).second) {
            issue(result.issues, row.line, "template_key", "厂家点稳定键重复");
        }
        if (!row.standard_code.empty() && !standard_codes.insert(row.standard_code).second) {
            issue(result.issues, row.line, "standard_code", "一个标准点不能绑定多个权威来源");
        }
        if (row.vendor_name.empty()) {
            issue(result.issues, row.line, "vendor_name", "厂家点名称不能为空");
        }
        if (row.access != "read" && row.access != "write") {
            issue(result.issues, row.line, "access", "只支持 read 或 write");
        }
        if (!valid_wire_type(row.wire_type)) {
            issue(result.issues, row.line, "wire_type", "不支持的 Modbus 数据类型");
        }
        if (!valid_space(row)) {
            issue(result.issues, row.line, "address_space", "地址空间与读写权限或数据类型不匹配");
        }
        if (!parse_uint16(get("address_offset"), row.address_offset)) {
            issue(result.issues, row.line, "address_offset", "必须是 0 到 65535 的零基偏移");
        }
        const bool wide =
            row.wire_type == "i32" || row.wire_type == "u32" || row.wire_type == "f32";
        if (wide && row.address_offset == 65535) {
            issue(result.issues, row.line, "address_offset", "双寄存器点越过地址空间末尾");
        }
        if (wide && row.byte_order != "ABCD" && row.byte_order != "BADC" &&
            row.byte_order != "CDAB" && row.byte_order != "DCBA") {
            issue(result.issues, row.line, "byte_order", "双寄存器点必须明确四字节顺序");
        }
        if (!wide && !row.byte_order.empty()) {
            issue(result.issues, row.line, "byte_order", "单寄存器或线圈点的字节序应为空");
        }
        if (!parse_finite(get("scale"), row.scale) || row.scale == 0.0) {
            issue(result.issues, row.line, "scale", "倍率必须是有限的非零数");
        }
        if (!parse_finite(get("offset"), row.offset)) {
            issue(result.issues, row.line, "offset", "偏移量必须是有限数");
        }
        if (row.wire_type == "bool" && (row.scale != 1.0 || row.offset != 0.0)) {
            issue(result.issues, row.line, "scale/offset", "布尔点不支持数值变换");
        }
        result.rows.push_back(std::move(row));
    }
    if (result.rows.empty() && result.issues.empty()) {
        issue(result.issues, 0, "file", "点表没有数据行");
    }
    return result;
}

PointTableImportResult import_point_table_file(const std::string& path,
                                               const core::DeviceId& device_id,
                                               const std::string& binding_id) {
    // 扩展名不涉及路径编码；CSV 兼容原有调用方的本地编码路径。
    auto extension = path.substr(
        path.find_last_of('.') == std::string::npos ? path.size() : path.find_last_of('.'));
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".xlsx") {
        PointTableImportResult result;
        result.device_id = device_id;
        result.binding_id = binding_id;
        std::size_t line = 0;
        std::string column = "file";
        try {
            if (std::filesystem::file_size(std::filesystem::u8path(path)) > kMaxFileBytes) {
                throw std::invalid_argument("点表文件超过 2 MiB");
            }
            util::Excel excel;
            excel.open(std::filesystem::u8path(path));
            const auto names = excel.sheetNames();
            const auto sheet = excel.hasSheet("点表")
                                   ? "点表"
                                   : (names.size() == 1 ? names.front() : std::string{});
            if (sheet.empty())
                throw std::invalid_argument(
                    "新格式 XLSX 需单工作表或名为点表的工作表；四遥模板请使用 "
                    "database::import_configuration_points");
            const auto [rows, columns] = excel.usedSize(sheet);
            if (rows > kMaxRows + 1 || columns > 64)
                throw std::invalid_argument("工作表超过 10000 数据行或 64 列");
            std::string csv;
            for (std::uint32_t r = 1; r <= rows; ++r) {
                line = r;
                for (std::uint16_t c = 1; c <= columns; ++c) {
                    std::string address;
                    for (auto n = c; n > 0; n /= 26) {
                        --n;
                        address.insert(address.begin(), static_cast<char>('A' + n % 26));
                    }
                    column = address + std::to_string(r);
                    if (!excel.readFormula(sheet, column).empty())
                        throw std::invalid_argument("点表不允许公式，请填入确定值");
                    auto value = excel.readCell(sheet, r, c);
                    auto text = std::visit(
                        [](const auto& v) -> std::string {
                            using T = std::decay_t<decltype(v)>;
                            if constexpr (std::is_same_v<T, std::monostate>)
                                return {};
                            else if constexpr (std::is_same_v<T, std::string>) {
                                if (v.find_first_of("\r\n") != std::string::npos)
                                    throw std::invalid_argument("单元格不允许换行");
                                return v;
                            } else if constexpr (std::is_same_v<T, bool>)
                                return v ? "1" : "0";
                            else {
                                std::ostringstream s;
                                s << std::setprecision(17) << v;
                                return s.str();
                            }
                        },
                        value);
                    if (c > 1) csv += ',';
                    csv += '"';
                    for (auto ch : text) {
                        if (ch == '"') csv += '"';
                        csv += ch;
                    }
                    csv += '"';
                }
                csv += '\n';
                if (csv.size() > kMaxFileBytes)
                    throw std::invalid_argument("展开后的点表超过 2 MiB");
            }
            return parse_point_table_csv(csv, device_id, binding_id);
        } catch (const std::exception& e) {
            issue(result.issues, line, column, e.what());
            return result;
        }
    }
    if (extension == ".xls") {
        PointTableImportResult result;
        result.device_id = device_id;
        result.binding_id = binding_id;
        issue(result.issues, 0, "file", "旧版 .xls 不受支持，请另存为 .xlsx");
        return result;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        PointTableImportResult result;
        result.device_id = device_id;
        result.binding_id = binding_id;
        issue(result.issues, 0, "file", "无法打开点表文件");
        return result;
    }
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(kMaxFileBytes)) {
        PointTableImportResult result;
        result.device_id = device_id;
        result.binding_id = binding_id;
        issue(result.issues, 0, "file", "点表文件超过 2 MiB 或不可读取");
        return result;
    }
    input.seekg(0);
    std::string text(static_cast<std::size_t>(size), '\0');
    input.read(text.data(), size);
    if (!input) {
        PointTableImportResult result;
        result.device_id = device_id;
        result.binding_id = binding_id;
        issue(result.issues, 0, "file", "读取点表文件失败");
        return result;
    }
    return parse_point_table_csv(text, device_id, binding_id);
}

PointTableLoadResult load_point_table(const model::ModelCatalog& catalog,
                                      const model::DevicePointDraft& draft,
                                      const PointTableImportResult& imported,
                                      PointTableLoadMode mode) {
    PointTableLoadResult result;
    result.catalog = catalog;
    result.draft = draft;
    if (!imported.ok()) {
        result.issues = imported.issues;
        return result;
    }
    if (imported.rows.empty()) {
        issue(result.issues, 0, "file", "点表没有数据行");
        return result;
    }
    if (imported.rows.size() > kMaxRows) {
        issue(result.issues, 0, "file", "点表行数超过 10000");
        return result;
    }
    std::unordered_set<std::string> imported_keys;
    std::unordered_set<std::string> imported_codes;
    for (const auto& row : imported.rows) {
        if (row.template_key.empty() || !imported_keys.insert(row.template_key).second) {
            issue(result.issues, row.line, "template_key", "厂家点稳定键为空或重复");
        }
        if (!row.standard_code.empty() && !imported_codes.insert(row.standard_code).second) {
            issue(result.issues, row.line, "standard_code", "标准点权威来源重复");
        }
        if (row.vendor_name.empty() || (row.access != "read" && row.access != "write") ||
            !valid_wire_type(row.wire_type) || !valid_space(row) || row.address_offset > 65535 ||
            !std::isfinite(row.scale) || row.scale == 0.0 || !std::isfinite(row.offset)) {
            issue(result.issues, row.line, "row", "厂家点字段未通过结构校验");
        }
        const bool wide =
            row.wire_type == "i32" || row.wire_type == "u32" || row.wire_type == "f32";
        if ((wide && (row.address_offset == 65535 ||
                      (row.byte_order != "ABCD" && row.byte_order != "BADC" &&
                       row.byte_order != "CDAB" && row.byte_order != "DCBA"))) ||
            (!wide && !row.byte_order.empty()) ||
            (row.wire_type == "bool" && (row.scale != 1.0 || row.offset != 0.0))) {
            issue(result.issues, row.line, "byte_order/scale", "字节序或布尔转换非法");
        }
    }
    if (!result.issues.empty()) return result;
    if (!catalog.indexed() || imported.device_id != draft.device_id) {
        issue(result.issues, 0, "device_id", "目录未建立索引或导入目标设备不一致");
        return result;
    }
    const auto* device = catalog.find_device(draft.device_id);
    const auto* binding = catalog.find_binding(imported.binding_id);
    const auto* channel = binding ? catalog.find_channel(binding->channel_id) : nullptr;
    if (device == nullptr || binding == nullptr || channel == nullptr ||
        binding->device_id != draft.device_id || !binding->enabled || !channel->enabled ||
        channel->profile.protocol.rfind("modbus", 0) != 0) {
        issue(result.issues, 0, "binding_id", "设备、Modbus 通道或绑定不存在或未启用");
        return result;
    }

    model::ModelCatalog candidate = catalog;
    model::DevicePointDraft candidate_draft = draft;
    std::unordered_set<std::string> point_ids;
    for (const auto& config : draft.points) point_ids.insert(config.point_id.value());
    if (mode == PointTableLoadMode::Replace) {
        auto& routes = candidate.vendor_routes;
        routes.erase(std::remove_if(routes.begin(), routes.end(),
                                    [&](const model::VendorPointRoute& r) {
                                        return point_ids.count(r.point_id.value()) != 0;
                                    }),
                     routes.end());
        for (auto& config : candidate_draft.points) {
            if (config.state == model::StandardPointState::Enabled) {
                config.state = model::StandardPointState::Unconfigured;
            }
        }
    }

    for (const auto& row : imported.rows) {
        if (row.standard_code.empty()) continue;
        auto point_config =
            std::find_if(candidate_draft.points.begin(), candidate_draft.points.end(),
                         [&](const model::DeviceStandardPointConfig& config) {
                             return config.standard_code == row.standard_code;
                         });
        if (point_config == candidate_draft.points.end()) {
            issue(result.issues, row.line, "standard_code", "设备固有点草稿中不存在该 code");
            continue;
        }
        if (point_config->state == model::StandardPointState::Disabled) {
            issue(result.issues, row.line, "standard_code", "固有点已显式禁用，不能由导入覆盖");
            continue;
        }
        const model::PointBase* point = catalog.find_point(point_config->point_id);
        if (point == nullptr || point->unit != row.unit) {
            issue(result.issues, row.line, "unit", "标准点不存在或标准单位不匹配");
            continue;
        }
        const bool writable = row.access == "write";
        const model::BindingOp operation =
            writable ? model::BindingOp::Control : model::BindingOp::Collect;
        if (!has_op(*binding, operation) ||
            (writable != model::is_command(point->point_type))) {
            issue(result.issues, row.line, "access", "点种类与通道读写操作不匹配");
            continue;
        }
        if ((row.wire_type == "bool") != (point->value_type == model::ValueType::Bool) ||
            (row.wire_type != "bool" && point->value_type != model::ValueType::Double)) {
            issue(result.issues, row.line, "wire_type", "厂家值类型无法直接映射到标准点");
            continue;
        }
        const bool already_bound =
            std::any_of(candidate.vendor_routes.begin(), candidate.vendor_routes.end(),
                        [&](const model::VendorPointRoute& route) {
                            return route.point_id == point->point_id && route.enabled;
                        });
        if (already_bound) {
            issue(result.issues, row.line, "standard_code", "标准点已有权威路由");
            continue;
        }
        const std::string route_id =
            "point-table:" + draft.device_id.value() + ":" + row.template_key;
        const bool duplicate_key =
            std::any_of(candidate.vendor_routes.begin(), candidate.vendor_routes.end(),
                        [&route_id](const model::VendorPointRoute& route) {
                            return route.route_id == route_id;
                        });
        if (duplicate_key) {
            issue(result.issues, row.line, "template_key", "设备内厂家点键已被使用");
            continue;
        }
        model::VendorPointRoute route;
        route.route_id = route_id;
        route.point_id = point->point_id;
        route.binding_id = imported.binding_id;
        route.template_key = row.template_key;
        route.vendor_name = row.vendor_name;
        route.protocol = "modbus";
        route.address_space = row.address_space;
        route.address_offset = row.address_offset;
        route.vendor_address =
            "modbus:" + row.address_space + ":" + std::to_string(row.address_offset);
        route.wire_type = row.wire_type;
        route.byte_order = row.byte_order;
        route.scale = row.scale;
        route.offset = row.offset;
        route.writable = writable;
        candidate.vendor_routes.push_back(std::move(route));
        point_config->state = model::StandardPointState::Enabled;
        point_config->disabled_reason.clear();
    }
    if (!result.issues.empty()) return result;

    const auto index_report = candidate.build_indexes();
    for (const auto& problem : index_report.issues) {
        issue(result.issues, 0, problem.field_path, problem.reason);
    }
    if (!result.issues.empty()) return result;
    const auto report = model::validate_device_point_draft(candidate, candidate_draft, false);
    for (const auto& problem : report.issues) {
        issue(result.issues, 0, problem.field_path, problem.reason);
    }
    if (!result.issues.empty()) return result;
    result.catalog = std::move(candidate);
    result.draft = std::move(candidate_draft);
    result.vendor_rows = imported.rows;
    result.applied = true;
    return result;
}

}  // namespace ems::config

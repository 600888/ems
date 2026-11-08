#include "telemetry_xlsx_parser.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace ems::web {
namespace {
std::string cell_text(const util::Excel& excel, const std::string& sheet, unsigned row,
                      unsigned column) {
    if (!column) return {};
    std::string address;
    for (auto n = column; n; n /= 26) {
        --n;
        address.insert(address.begin(), static_cast<char>('A' + n % 26));
    }
    if (!excel.readFormula(sheet, address + std::to_string(row)).empty())
        throw std::invalid_argument("不允许公式，请填写确定值");
    return std::visit(
        [](const auto& value) -> std::string {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return {};
            else if constexpr (std::is_same_v<T, std::string>)
                return value;
            else if constexpr (std::is_same_v<T, bool>)
                return value ? "1" : "0";
            else
                return Json(value).dump();
        },
        excel.readCell(sheet, row, static_cast<std::uint16_t>(column)));
}

double numeric(const std::string& text, double fallback) {
    if (text.empty()) return fallback;
    std::size_t used = 0;
    double result = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(result))
        throw std::invalid_argument("必须为有限数值");
    return result;
}

std::uint64_t integer(const std::string& text, std::uint64_t maximum) {
    if (text.empty() || text.front() == '-') throw std::invalid_argument("缺失或无效整数");
    std::size_t used = 0;
    const int base =
        text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X') ? 16 : 10;
    auto result = std::stoull(text, &used, base);
    if (used != text.size()) {
        double value = numeric(text, -1);
        if (value < 0 || value > static_cast<double>(maximum) || std::floor(value) != value)
            throw std::invalid_argument("整数超出范围");
        return static_cast<std::uint64_t>(value);
    }
    if (result > maximum) throw std::invalid_argument("整数超出范围");
    return result;
}
}  // namespace

Json parse_telemetry_xlsx(const util::Excel& excel, const std::string& protocol) {
    Json result = {
        {"rows", Json::array()}, {"issues", Json::array()}, {"source_kind", "telemetry_xlsx"}};
    const std::map<std::string, std::string> kinds{
        {"遥测", "yc"}, {"遥信", "yx"}, {"遥控", "yk"}, {"遥调", "yt"}};
    const std::set<std::string> common{"测点编码", "测点名称", "从机地址",    "寄存器地址",
                                       "功能码",   "解析码",   "104ASDU类型", "单位"};
    std::set<std::string> keys;
    int line = 0;
    for (const auto& name : excel.sheetNames()) {
        if (!kinds.count(name)) throw std::invalid_argument("四遥工作簿包含不支持的工作表");
    }
    for (const auto& name : {"遥测", "遥信", "遥控", "遥调"}) {
        if (!excel.hasSheet(name)) continue;
        const auto kind = kinds.at(name);
        const auto [rows, columns] = excel.usedSize(name);
        if (rows > 10001 || columns > 64) throw std::invalid_argument("四遥工作表超过行列上限");
        std::map<std::string, unsigned> headers;
        auto allowed = common;
        for (auto field : {"乘系数", "加系数", "上限值", "下限值", "位偏移", "是否反转", "命令类型",
                           "关联遥信编码", "关联遥测编码"})
            allowed.insert(field);
        for (unsigned c = 1; c <= columns; ++c) {
            const auto header = cell_text(excel, name, 1, c);
            // 示例文件末尾含只有格式的空列。
            if (header.empty()) continue;
            if (!allowed.count(header) || !headers.emplace(header, c).second)
                throw std::invalid_argument(std::string(name) + "：重复或不支持的表头 " + header);
        }
        for (auto required : {"测点编码", "测点名称", "寄存器地址"})
            if (!headers.count(required))
                throw std::invalid_argument(std::string(name) + "：缺少表头 " + required);
        for (unsigned r = 2; r <= rows; ++r) {
            std::string field;
            auto get = [&](const std::string& key) {
                field = key;
                return cell_text(excel, name, r, headers.count(key) ? headers.at(key) : 0);
            };
            bool blank = true;
            try {
                for (unsigned c = 1; c <= columns; ++c) {
                    auto value = cell_text(excel, name, r, c);
                    if (!value.empty()) {
                        blank = false;
                        bool named = false;
                        for (const auto& h : headers) named = named || h.second == c;
                        if (!named) throw std::invalid_argument("空表头列中存在数据");
                    }
                }
                if (blank) continue;
                if (++line > 10000) throw std::invalid_argument("整表超过 10000 个点");
                const auto key = get("测点编码");
                if (key.empty() || key.size() > 128 || !keys.insert(key).second)
                    throw std::invalid_argument("测点编码缺失、过长或重复");
                const auto label = get("测点名称");
                if (label.empty() || label.size() > 128)
                    throw std::invalid_argument("测点名称无效");
                Json extra = Json::object();
                Json options = {{"protocol", protocol}, {"sheet", name}, {"row", r}};
                const auto slave = get("从机地址");
                if (!slave.empty()) options["device_address"] = slave;
                Json row = {{"line_no", line},
                            {"vendor_key", key},
                            {"vendor_name", label},
                            {"semantic_code", ""},
                            {"point_type", kind},
                            {"address_space", ""},
                            {"address_offset", 0},
                            {"bit_offset", 0},
                            {"register_count", 1},
                            {"wire_type", "u16"},
                            {"byte_order", ""},
                            {"value_type", (kind == "yx" || kind == "yk") ? "bool" : "double"},
                            {"access", (kind == "yk" || kind == "yt") ? "write" : "read"},
                            {"scale_factor", 1.0},
                            {"offset_value", 0.0},
                            {"unit", ""}};
                row["unit"] = get("单位");
                const auto address = get("寄存器地址");
                if (protocol == "iec104") {
                    extra["ioa"] = integer(address, 0xffffff);
                    if (!slave.empty()) {
                        auto ca = integer(slave, 65535);
                        if (!ca) throw std::invalid_argument("公共地址必须为 1–65535");
                        extra["casdu"] = ca;
                        options["device_address"] = std::to_string(ca);
                    }
                    const auto type = get("104ASDU类型");
                    const std::map<std::string, std::set<std::string>> types{
                        {"yc",
                         {"M_ME_NA_1", "M_ME_NB_1", "M_ME_NC_1", "M_ME_TD_1", "M_ME_TE_1",
                          "M_ME_TF_1"}},
                        {"yx", {"M_SP_NA_1", "M_DP_NA_1", "M_SP_TB_1", "M_DP_TB_1"}},
                        {"yk", {"C_SC_NA_1", "C_DC_NA_1"}},
                        {"yt", {"C_SE_NA_1", "C_SE_NB_1", "C_SE_NC_1"}}};
                    if (!types.at(kind).count(type))
                        throw std::invalid_argument("缺失或不匹配的 ASDU 类型");
                    extra["iec104_type"] = type;
                    row["wire_type"] = kind == "yc" || kind == "yt" ? "f32" : "bool";
                    if ((kind == "yc" || kind == "yt") && type != "M_ME_NC_1" &&
                        type != "M_ME_TF_1" && type != "C_SE_NC_1")
                        row["wire_type"] = "i16";
                    if (type == "M_DP_NA_1" || type == "M_DP_TB_1" || type == "C_DC_NA_1") {
                        row["wire_type"] = "u16";
                        row["value_type"] = "int64";
                    }
                } else if (protocol == "dlt645") {
                    if (kind != "yc") throw std::invalid_argument("DLT645 当前只支持遥测");
                    integer(address, 0xffffffff);
                    extra["data_id"] = address;
                    // 模板旧从机编号不是 12 位电表地址，实际表号以绑定为准。
                    options.erase("device_address");
                    row["wire_type"] = "u32";
                } else if (protocol == "modbus") {
                    row["address_offset"] = integer(address, 65535);
                    const auto function = integer(get("功能码"), 16);
                    const std::map<int, std::string> spaces{{1, "coil"},
                                                            {2, "discrete_input"},
                                                            {3, "holding_register"},
                                                            {4, "input_register"},
                                                            {5, "coil"},
                                                            {6, "holding_register"},
                                                            {15, "coil"},
                                                            {16, "holding_register"}};
                    if (!spaces.count(static_cast<int>(function)) ||
                        ((kind == "yk" || kind == "yt") ? function < 5 : function > 4))
                        throw std::invalid_argument("功能码与四遥类别不匹配");
                    row["address_space"] = spaces.at(static_cast<int>(function));
                    extra["function_code"] = function;
                    const auto decode = get("解析码");
                    const std::map<std::string, std::string> wires{
                        {"UINT16", "u16"}, {"INT16", "i16"},   {"UINT32", "u32"},
                        {"INT32", "i32"},  {"FLOAT32", "f32"}, {"FLOAT", "f32"}};
                    auto split = decode.find('_');
                    auto type = decode.substr(0, split);
                    if (!wires.count(type)) throw std::invalid_argument("不支持的解析码");
                    row["wire_type"] = wires.at(type);
                    auto order = split == std::string::npos ? "AB" : decode.substr(split + 1);
                    const bool wide =
                        type == "UINT32" || type == "INT32" || type == "FLOAT32" || type == "FLOAT";
                    if (wide && (kind == "yx" || function == 1 || function == 2 || function == 5 ||
                                 function == 15))
                        throw std::invalid_argument("位信号只能使用单寄存器解析码");
                    if (wide) {
                        if (order != "ABCD" && order != "BADC" && order != "CDAB" &&
                            order != "DCBA")
                            throw std::invalid_argument("双寄存器字节序无效");
                        row["byte_order"] = order;
                        row["register_count"] = 2;
                    } else {
                        if (order != "AB" && order != "BA")
                            throw std::invalid_argument("单寄存器字节序无效");
                        row["byte_order"] = order == "BA" ? "ba" : "";
                    }
                    if (row.at("address_offset").get<int>() + row.at("register_count").get<int>() >
                        65536)
                        throw std::invalid_argument("寄存器跨越地址上限");
                    if (kind == "yx" || function == 1 || function == 2 || function == 5 ||
                        function == 15)
                        row["wire_type"] = "bool";
                    if (kind == "yk" && row.at("wire_type") != "bool") row["value_type"] = "int64";
                    auto bit = get("位偏移");
                    row["bit_offset"] = bit.empty() ? 0 : integer(bit, 15);
                    if (!slave.empty()) {
                        auto unit = integer(slave, 247);
                        if (!unit) throw std::invalid_argument("从机地址必须为 1–247");
                        options["device_address"] = std::to_string(unit);
                    }
                } else {
                    throw std::invalid_argument("不支持的四遥点表协议");
                }
                if (kind == "yc" || kind == "yt") {
                    row["scale_factor"] = numeric(get("乘系数"), 1);
                    row["offset_value"] = numeric(get("加系数"), 0);
                    if (row.at("scale_factor") == 0) throw std::invalid_argument("乘系数不能为 0");
                    for (const auto& item :
                         {std::pair{"上限值", "range_max"}, std::pair{"下限值", "range_min"}}) {
                        auto value = get(item.first);
                        if (!value.empty()) options[item.second] = numeric(value, 0);
                    }
                    if (options.value("range_min", -9999999.0) >
                        options.value("range_max", 9999999.0))
                        throw std::invalid_argument("下限大于上限");
                }
                if (kind == "yx") {
                    auto invert = get("是否反转");
                    options["invert"] = invert.empty() ? 0 : integer(invert, 1);
                }
                if (kind == "yk" || kind == "yt") {
                    auto feedback = get(kind == "yk" ? "关联遥信编码" : "关联遥测编码");
                    if (!feedback.empty()) options["feedback_key"] = feedback;
                    if (kind == "yk") options["command_type"] = get("命令类型");
                }
                extra["_import"] = options;
                row["protocol_address_json"] = extra.dump();
                result["rows"].push_back(std::move(row));
            } catch (const std::exception& error) {
                result["issues"].push_back({{"line_no", line},
                                            {"sheet", name},
                                            {"row", r},
                                            {"field", field},
                                            {"message", error.what()}});
            }
        }
    }
    for (const auto& row : result.at("rows")) {
        auto extra = Json::parse(row.at("protocol_address_json").get<std::string>());
        const auto& options = extra.at("_import");
        if (!options.contains("feedback_key")) continue;
        auto found =
            std::find_if(result["rows"].begin(), result["rows"].end(), [&](const Json& other) {
                return other.at("vendor_key") == options.at("feedback_key") &&
                       other.at("point_type") == (row.at("point_type") == "yk" ? "yx" : "yc");
            });
        if (found == result["rows"].end())
            result["issues"].push_back({{"line_no", row.at("line_no")},
                                        {"field", "feedback"},
                                        {"message", "关联反馈点缺失或类别不匹配"}});
    }
    if (result.at("rows").empty() && result.at("issues").empty())
        result["issues"].push_back(
            {{"line_no", 0}, {"field", "workbook"}, {"message", "点表没有数据行"}});
    return result;
}
}  // namespace ems::web

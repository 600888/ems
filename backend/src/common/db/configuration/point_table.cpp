#include <common/database/configuration/point_table.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <type_traits>

#include "util/excel.hpp"

namespace ems::database {
namespace {

constexpr std::size_t kMaxRows = 10000;

std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::string cell_address(std::uint32_t row, std::uint16_t column) {
    std::string address;
    while (column) {
        --column;
        address.insert(address.begin(), static_cast<char>('A' + column % 26));
        column /= 26;
    }
    return address + std::to_string(row);
}

std::string cell_text(const util::ExcelValue& value) {
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return {};
            else if constexpr (std::is_same_v<T, std::string>)
                return trim(v);
            else if constexpr (std::is_same_v<T, bool>)
                return v ? "1" : "0";
            else {
                std::ostringstream stream;
                stream << std::setprecision(std::numeric_limits<double>::max_digits10) << v;
                return stream.str();
            }
        },
        value);
}

int64_t integer(const std::string& text, int64_t min, int64_t max) {
    if (text.empty()) throw std::invalid_argument("必须填写整数");
    const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    const auto digits = hex ? text.substr(2) : text;
    if (digits.empty() || digits.find_first_not_of(hex ? "0123456789abcdefABCDEF" : "0123456789") !=
                              std::string::npos) {
        throw std::invalid_argument("必须是十进制整数或 0x 开头的十六进制整数");
    }
    const auto value = std::stoull(digits, nullptr, hex ? 16 : 10);
    if (value > static_cast<uint64_t>(max) || value < static_cast<uint64_t>(min)) {
        throw std::invalid_argument("整数超出允许范围");
    }
    return static_cast<int64_t>(value);
}

double number(const std::string& text) {
    if (text.empty()) throw std::invalid_argument("必须填写数值");
    std::size_t consumed = 0;
    const auto value = std::stod(text, &consumed);
    if (consumed != text.size() || !std::isfinite(value)) {
        throw std::invalid_argument("必须是有限数值");
    }
    return value;
}

std::pair<int, int> decode(std::string text) {
    static const std::map<std::string, int> types = {
        {"UINT16", 0}, {"INT16", 1},   {"UINT32", 2}, {"INT32", 3}, {"FLOAT", 4}, {"FLOAT32", 4},
        {"DOUBLE", 5}, {"FLOAT64", 5}, {"UINT8", 6},  {"INT8", 7},  {"BOOL", 8},  {"BIT", 9}};
    static const std::map<std::string, int> orders = {
        {"AB", 0},   {"BA", 2},       {"ABCD", 0},     {"CDAB", 1},     {"BADC", 2},
        {"DCBA", 3}, {"ABCDEFGH", 0}, {"GHEFCDAB", 1}, {"BADCFEHG", 2}, {"HGFEDCBA", 3}};
    if (text.find_first_not_of("0123456789") == std::string::npos && !text.empty()) {
        return {static_cast<int>(integer(text, 0, 9)), 0};
    }
    const auto split = text.find('_');
    const auto type = types.find(text.substr(0, split));
    if (type == types.end()) throw std::invalid_argument("不支持的解析码");
    int order = 0;
    if (split != std::string::npos) {
        const auto suffix = text.substr(split + 1);
        const auto found = orders.find(suffix);
        const auto width = type->second == 5 ? 8 : (type->second >= 2 && type->second <= 4 ? 4 : 2);
        if (found == orders.end() || suffix.size() != static_cast<std::size_t>(width)) {
            throw std::invalid_argument("解析码字节序与数据宽度不匹配");
        }
        order = found->second;
    }
    return {type->second, order};
}

}  // namespace

PointTableImportResult parse_point_table_excel(const std::string& path, int protocol_type,
                                               int64_t channel_id, int64_t device_id) {
    PointTableImportResult result;
    std::string sheet;
    std::size_t line = 0;
    std::string column = "file";
    auto error = [&](std::string reason) {
        if (result.errors.size() < 256)
            result.errors.push_back({sheet, line, column, std::move(reason)});
    };
    if (channel_id <= 0 || device_id <= 0) {
        error("必须先选择实际设备和通道");
        return result;
    }
    if (protocol_type < 0 || protocol_type > 3) {
        error("当前 Excel 导入只支持 DLT645、Modbus RTU/TCP 和 IEC104");
        return result;
    }
    try {
        auto file = std::filesystem::u8path(path);
        auto extension = file.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".xlsx")
            throw std::invalid_argument("请使用 .xlsx 文件，旧版 .xls 不受支持");
        if (std::filesystem::file_size(file) > 2 * 1024 * 1024) {
            throw std::invalid_argument("点表文件超过 2 MiB");
        }
        util::Excel excel;
        excel.open(file);
        const std::vector<std::string> sheets = {"遥测", "遥信", "遥控", "遥调"};
        std::set<std::string> codes;
        std::size_t total = 0;
        for (std::size_t kind = 0; kind < sheets.size(); ++kind) {
            sheet = sheets[kind];
            line = 1;
            column = "sheet";
            if (!excel.hasSheet(sheet)) {
                error("缺少四遥工作表");
                continue;
            }
            const auto [rows, cols] = excel.usedSize(sheet);
            if (rows > kMaxRows + 1 || cols > 64) {
                error("工作表超过 10000 数据行或 64 列");
                continue;
            }
            std::map<std::string, std::uint16_t> headers;
            auto read = [&](std::uint32_t r, std::uint16_t c) {
                if (!excel.readFormula(sheet, cell_address(r, c)).empty()) {
                    throw std::invalid_argument("点表不允许公式，请填入确定值");
                }
                return cell_text(excel.readCell(sheet, r, c));
            };
            const auto before = result.errors.size();
            for (std::uint16_t c = 1; c <= cols; ++c) {
                column = cell_address(1, c);
                try {
                    auto name = read(1, c);
                    if (name.empty()) continue;
                    if (!headers.emplace(name, c).second) error("表头重复");
                    if (name == "NodeId" || name == "DNP3点位类型")
                        error("协议专用模板与所选通道不兼容");
                } catch (const std::exception& e) {
                    error(e.what());
                }
            }
            std::vector<std::string> required = {"测点编码", "测点名称", "从机地址", "寄存器地址"};
            if (protocol_type == 1 || protocol_type == 2) {
                required.insert(required.end(), {"功能码", "解析码"});
            }
            if (kind == 0 || kind == 3) {
                required.insert(required.end(), {"乘系数", "加系数", "上限值", "下限值"});
            } else {
                required.push_back("位偏移");
                required.push_back(kind == 1 ? "是否反转" : "命令类型");
            }
            if (kind == 2) required.push_back("关联遥信编码");
            if (kind == 3) required.push_back("关联遥测编码");
            if (protocol_type == 3) required.push_back("104ASDU类型");
            for (const auto& name : required) {
                column = name;
                if (!headers.count(name)) error("缺少必填列");
            }
            if (before != result.errors.size()) continue;
            for (std::uint32_t r = 2; r <= rows; ++r) {
                line = r;
                try {
                    std::map<std::string, std::string> values;
                    bool nonempty = false;
                    for (const auto& [name, c] : headers) {
                        column = name;
                        values[name] = read(r, c);
                        nonempty |= !values[name].empty();
                    }
                    if (!nonempty) continue;
                    if (protocol_type == 0 && kind != 0) {
                        throw std::invalid_argument("DLT645 模板当前只支持遥测数据标识");
                    }
                    if (++total > kMaxRows) throw std::invalid_argument("整表超过 10000 个点");
                    auto get = [&](const std::string& name) -> std::string {
                        column = name;
                        return values[name];
                    };
                    auto init = [&](auto& point) {
                        point.code = get("测点编码");
                        if (point.code.empty() || point.code.size() > 128 ||
                            point.code.find('\0') != std::string::npos ||
                            !codes.insert(point.code).second) {
                            throw std::invalid_argument(
                                "测点编码为空、重复、含空字符或超过 128 字节");
                        }
                        point.name = get("测点名称");
                        if (point.name.empty() || point.name.size() > 128 ||
                            point.name.find('\0') != std::string::npos) {
                            throw std::invalid_argument("测点名称为空、含空字符或超过 128 字节");
                        }
                        point.device_id = device_id;
                        point.channel_id = channel_id;
                        point.protocol_type = protocol_type;
                        const auto slave =
                            integer(get("从机地址"), 1, protocol_type == 3 ? 65535 : 247);
                        const auto address =
                            integer(get("寄存器地址"), 0,
                                    protocol_type == 0 ? 0xffffffffLL
                                                       : (protocol_type == 3 ? 0xffffff : 65535));
                        if (protocol_type == 0) {
                            point.slave_id = static_cast<int>(slave);
                            point.data_id = address;
                        } else if (protocol_type == 3) {
                            point.casdu = static_cast<int>(slave);
                            point.ioa = address;
                        } else {
                            point.slave_id = static_cast<int>(slave);
                            point.reg_addr = static_cast<int>(address);
                        }
                        if (protocol_type == 1 || protocol_type == 2) {
                            point.func_code = static_cast<int>(integer(get("功能码"), 1, 16));
                            const auto f = point.func_code;
                            if ((kind < 2 && f != 1 && f != 2 && f != 3 && f != 4) ||
                                (kind >= 2 && f != 5 && f != 6 && f != 15 && f != 16)) {
                                throw std::invalid_argument("功能码与工作表读写方向不匹配");
                            }
                        }
                        auto decoder = get("解析码");
                        if (!decoder.empty() || protocol_type == 1 || protocol_type == 2) {
                            const auto [type, order] = decode(decoder);
                            point.decode_code = type;
                            point.endian = order;
                        }
                        if ((protocol_type == 1 || protocol_type == 2) && point.func_code != 1 &&
                            point.func_code != 2 && point.func_code != 5 && point.func_code != 15) {
                            const auto width =
                                point.decode_code == 5
                                    ? 4
                                    : (point.decode_code >= 2 && point.decode_code <= 4 ? 2 : 1);
                            if (address + width > 65536)
                                throw std::invalid_argument("点位跨越寄存器地址空间末尾");
                        }
                        point.iec104_type = get("104ASDU类型");
                        if (!point.iec104_type.empty() && protocol_type != 3) {
                            throw std::invalid_argument("104ASDU类型只能用于 IEC104 通道");
                        }
                        if (protocol_type == 3) {
                            const std::vector<std::set<std::string>> types = {
                                {"M_ME_NA_1", "M_ME_NB_1", "M_ME_NC_1", "M_ME_TD_1", "M_ME_TE_1",
                                 "M_ME_TF_1"},
                                {"M_SP_NA_1", "M_DP_NA_1", "M_SP_TB_1", "M_DP_TB_1"},
                                {"C_SC_NA_1", "C_DC_NA_1", "C_SC_TA_1", "C_DC_TA_1"},
                                {"C_SE_NA_1", "C_SE_NB_1", "C_SE_NC_1", "C_SE_TA_1", "C_SE_TB_1",
                                 "C_SE_TC_1"}};
                            if (!types[kind].count(point.iec104_type))
                                throw std::invalid_argument("ASDU 类型与工作表不匹配或不受支持");
                        }
                    };
                    auto numeric = [&](auto& point) {
                        point.mul_coe = number(get("乘系数"));
                        if (point.mul_coe == 0) throw std::invalid_argument("乘系数不能为零");
                        point.add_coe = number(get("加系数"));
                        point.min_limit = number(get("下限值"));
                        point.max_limit = number(get("上限值"));
                        if (point.min_limit > point.max_limit)
                            throw std::invalid_argument("下限值大于上限值");
                        point.unit = get("单位");
                        if (point.unit.size() > 32) throw std::invalid_argument("单位超过 32 字节");
                    };
                    if (kind == 0) {
                        AnalogPoint p;
                        init(p);
                        numeric(p);
                        result.data.yc.push_back(std::move(p));
                    } else if (kind == 1) {
                        StatusPoint p;
                        init(p);
                        p.bit = p.bit_position = static_cast<int>(integer(get("位偏移"), 0, 15));
                        p.reverse_flag = static_cast<int>(integer(get("是否反转"), 0, 1));
                        result.data.yx.push_back(std::move(p));
                    } else if (kind == 2) {
                        ControlPoint p;
                        init(p);
                        p.bit = p.bit_position = static_cast<int>(integer(get("位偏移"), 0, 15));
                        p.command_type = static_cast<int>(integer(get("命令类型"), 0, 1));
                        result.data.yk_feedback[p.code] = get("关联遥信编码");
                        result.data.yk.push_back(std::move(p));
                    } else {
                        SetpointPoint p;
                        init(p);
                        numeric(p);
                        result.data.yt_feedback[p.code] = get("关联遥测编码");
                        result.data.yt.push_back(std::move(p));
                    }
                } catch (const std::exception& e) {
                    error(e.what());
                }
            }
        }
        if (total == 0 && result.errors.empty()) {
            line = 0;
            column = "file";
            error("点表没有数据行");
        }
    } catch (const std::exception& e) {
        error(e.what());
    }
    return result;
}

}  // namespace ems::database

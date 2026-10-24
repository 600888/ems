#include "point_table_parser.h"

#include <common/config/point_table.h>
#include <miniz.h>

#include <filesystem>
#include <fstream>
#include <util/excel.hpp>

#include "web/platform/crypto.h"

namespace ems::web {
Json parse_vendor_xlsx(const std::string& bytes, const std::string& workspace) {
    // 在交给 Excel 库之前核对 ZIP 目录的总解压预算，阻止压缩炸弹。
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0))
        throw ApiError(422, "InvalidArgument", "invalid_xlsx_zip");
    std::uint64_t expanded = 0;
    bool safe = mz_zip_reader_get_num_files(&zip) <= 10000;
    for (mz_uint i = 0; safe && i < mz_zip_reader_get_num_files(&zip); ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat) || stat.m_uncomp_size > 100 * 1024 * 1024) {
            safe = false;
            break;
        }
        expanded += stat.m_uncomp_size;
        safe = expanded <= 100 * 1024 * 1024;
    }
    mz_zip_reader_end(&zip);
    if (!safe) throw ApiError(413, "ResourceExhausted", "xlsx_expanded_limit");

    std::filesystem::create_directories(std::filesystem::u8path(workspace));
    auto path = std::filesystem::u8path(workspace) / (random_token() + ".xlsx");

    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } cleanup{path};

    {
        std::ofstream output(path, std::ios::binary);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!output) throw ApiError(503, "ResourceExhausted", "upload_workspace_failed");
    }
    {
        util::Excel excel;
        excel.open(path);
        const auto names = excel.sheetNames();
        const auto sheet = excel.hasSheet("点表")
                               ? std::string("点表")
                               : (names.size() == 1 ? names.front() : std::string{});
        if (sheet.empty()) throw ApiError(422, "Unsupported", "xlsx_sheet_not_supported");
        const auto size = excel.usedSize(sheet);
        if (size.first > 10001 || size.second > 64)
            throw ApiError(413, "ResourceExhausted", "xlsx_sheet_limit");
        const std::set<std::string> supported = {
            "template_key",   "standard_code", "vendor_name", "address_space",
            "address_offset", "access",        "wire_type",   "byte_order",
            "scale",          "offset",        "unit"};
        for (std::uint16_t column = 1; column <= size.second; ++column) {
            auto cell = excel.readCell(sheet, 1, column);
            if (!std::holds_alternative<std::string>(cell) ||
                !supported.count(std::get<std::string>(cell)))
                throw ApiError(422, "Unsupported", "import_column_not_supported");
        }
    }
    config::PointTableImportResult parsed =
        config::import_point_table_file(path.u8string(), core::DeviceId(""), "");
    Json result = {{"rows", Json::array()}, {"issues", Json::array()}};
    for (const auto& row : parsed.rows)
        result["rows"].push_back({{"line_no", row.line},
                                  {"vendor_key", row.template_key},
                                  {"vendor_name", row.vendor_name},
                                  {"semantic_code", row.standard_code},
                                  {"address_space", row.address_space},
                                  {"address_offset", row.address_offset},
                                  {"access", row.access},
                                  {"wire_type", row.wire_type},
                                  {"byte_order", row.byte_order},
                                  {"scale_factor", row.scale},
                                  {"offset_value", row.offset},
                                  {"unit", row.unit}});
    for (const auto& issue : parsed.issues)
        result["issues"].push_back(
            {{"line_no", issue.line}, {"field", issue.column}, {"message", issue.reason}});
    return result;
}

Json parse_upload(const Json& metadata, const Upload& upload, const std::string& workspace) {
    fields(metadata, {"device_id", "binding_id", "source_kind", "address_base"});
    const auto device = text(metadata, "device_id");
    const auto binding = text(metadata, "binding_id");
    const auto source = text(metadata, "source_kind");
    const auto base = text(metadata, "address_base");
    if (base != "zero" && base != "one")
        throw ApiError(422, "InvalidArgument", "address_base_invalid");
    if (upload.bytes.empty() || upload.bytes.size() > 2 * 1024 * 1024)
        throw ApiError(413, "ResourceExhausted", "parser_file_limit");
    if (upload.filename.size() > 255) throw ApiError(400, "InvalidArgument", "filename_too_long");
    config::PointTableImportResult parsed;

    if (source == "vendor_csv") {
        // 不忽略专有扩展列，防止文件看似导入成功却遗失写入规则。
        auto first = upload.bytes.substr(0, upload.bytes.find_first_of("\r\n"));
        if (first.compare(0, 3, "\xEF\xBB\xBF") == 0) first.erase(0, 3);
        const std::set<std::string> supported = {
            "template_key",   "standard_code", "vendor_name", "address_space",
            "address_offset", "access",        "wire_type",   "byte_order",
            "scale",          "offset",        "unit"};
        std::size_t start = 0;
        while (start <= first.size()) {
            auto end = first.find(',', start);
            if (end == std::string::npos) end = first.size();
            auto column = first.substr(start, end - start);
            if (column.size() > 1 && column.front() == '"' && column.back() == '"')
                column = column.substr(1, column.size() - 2);
            if (!supported.count(column))
                throw ApiError(422, "Unsupported", "import_column_not_supported");
            if (end == first.size()) break;
            start = end + 1;
        }
        parsed = config::parse_point_table_csv(upload.bytes, core::DeviceId(device), binding);
    } else if (source == "vendor_xlsx") {
        parsed.rows.clear();
        Json preview = parse_vendor_xlsx(upload.bytes, workspace);
        for (const auto& row : preview.at("rows")) {
            config::PointTableRow restored;
            restored.line = row.at("line_no").get<int>();
            restored.template_key = row.at("vendor_key").get<std::string>();
            restored.vendor_name = row.at("vendor_name").get<std::string>();
            restored.standard_code = row.at("semantic_code").get<std::string>();
            restored.address_space = row.at("address_space").get<std::string>();
            restored.address_offset = row.at("address_offset").get<int>();
            restored.access = row.at("access").get<std::string>();
            restored.wire_type = row.at("wire_type").get<std::string>();
            restored.byte_order = row.at("byte_order").get<std::string>();
            restored.scale = row.at("scale_factor").get<double>();
            restored.offset = row.at("offset_value").get<double>();
            restored.unit = row.at("unit").get<std::string>();
            parsed.rows.push_back(std::move(restored));
        }
    } else
        throw ApiError(422, "Unsupported", "import_format_not_supported");

    Json result = {{"rows", Json::array()}, {"issues", Json::array()}};
    for (const auto& row : parsed.rows)
        result["rows"].push_back({{"line_no", row.line},
                                  {"vendor_key", row.template_key},
                                  {"vendor_name", row.vendor_name},
                                  {"semantic_code", row.standard_code},
                                  {"address_space", row.address_space},
                                  {"address_offset", row.address_offset},
                                  {"access", row.access},
                                  {"wire_type", row.wire_type},
                                  {"byte_order", row.byte_order},
                                  {"scale_factor", row.scale},
                                  {"offset_value", row.offset},
                                  {"unit", row.unit}});
    for (const auto& issue : parsed.issues)
        result["issues"].push_back(
            {{"line_no", issue.line}, {"field", issue.column}, {"message", issue.reason}});
    return result;
}
}  // namespace ems::web

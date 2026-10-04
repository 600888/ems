#include <OpenXLSX.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <pugixml.hpp>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <util/excel.hpp>

namespace ems::util {
namespace {

constexpr std::uint32_t kMaxRows = 1048576;
constexpr std::uint16_t kMaxColumns = 16384;

// 在内存中适配合法的 OOXML 前缀和非数字关系 ID，不改写源文件。
class CompatibleArchive {
   public:
    bool isValid() const { return archive_.isValid(); }

    bool isOpen() const { return archive_.isOpen(); }

    void open(const std::string& path) { archive_.open(path); }

    void close() { archive_.close(); }

    void save(const std::string& path) { archive_.save(path); }

    void addEntry(const std::string& name, const std::string& data) {
        archive_.addEntry(name, data);
    }

    void addEntryAndCommit(const std::string& name, const std::string& data) {
        archive_.addEntryAndCommit(name, data);
    }

    void deleteEntry(const std::string& name) { archive_.deleteEntry(name); }

    bool hasEntry(const std::string& name) const { return archive_.hasEntry(name); }

    std::string getEntry(const std::string& name) {
        auto data = archive_.getEntry(name);
        if (data.empty()) return data;  // OpenXLSX 为缺失的属性部件先创建空条目。
        const bool xml = name.size() >= 4 && name.substr(name.size() - 4) == ".xml";
        const bool rels = name.size() >= 5 && name.substr(name.size() - 5) == ".rels";
        if (!xml && !rels) return data;
        pugi::xml_document document;
        const auto parsed = document.load_buffer(data.data(), data.size());
        if (!parsed) {
            if (parsed.status == pugi::status_no_document_element &&
                name.compare(0, 9, "docProps/") == 0)
                return data;
            throw std::runtime_error("Invalid XLSX XML part: " + name);
        }
        std::map<std::string, std::string> relations;
        if (rels) {
            std::size_t index = 0;
            for (auto child : document.document_element().children()) {
                if (child.type() == pugi::node_element)
                    relations[child.attribute("Id").value()] = "rId" + std::to_string(++index);
            }
        } else {
            const auto slash = name.find_last_of('/');
            const auto relname = (slash == std::string::npos ? "" : name.substr(0, slash + 1)) +
                                 "_rels/" +
                                 name.substr(slash == std::string::npos ? 0 : slash + 1) + ".rels";
            if (archive_.hasEntry(relname)) {
                const auto raw = archive_.getEntry(relname);
                pugi::xml_document rel;
                if (!rel.load_buffer(raw.data(), raw.size()))
                    throw std::runtime_error("Invalid XLSX relationships");
                std::size_t index = 0;
                for (auto child : rel.document_element().children()) {
                    if (child.type() == pugi::node_element)
                        relations[child.attribute("Id").value()] = "rId" + std::to_string(++index);
                }
            }
        }
        normalize(document.document_element(), {}, relations, rels);
        std::ostringstream output;
        document.save(output, "", pugi::format_raw, pugi::encoding_utf8);
        return output.str();
    }

   private:
    static void normalize(pugi::xml_node node, std::map<std::string, std::string> namespaces,
                          const std::map<std::string, std::string>& relations, bool rels) {
        if (node.type() != pugi::node_element) return;
        for (auto attr : node.attributes()) {
            const auto name = std::string(attr.name());
            if (name == "xmlns")
                namespaces[""] = attr.value();
            else if (name.compare(0, 6, "xmlns:") == 0)
                namespaces[name.substr(6)] = attr.value();
        }
        const auto name = std::string(node.name());
        const auto colon = name.find(':');
        const auto prefix = colon == std::string::npos ? "" : name.substr(0, colon);
        const auto uri = namespaces[prefix];
        if (colon != std::string::npos &&
            (uri == "http://schemas.openxmlformats.org/spreadsheetml/2006/main" ||
             uri == "http://schemas.openxmlformats.org/package/2006/relationships" ||
             uri == "http://schemas.openxmlformats.org/package/2006/content-types" ||
             uri == "http://schemas.openxmlformats.org/officeDocument/2006/extended-properties")) {
            node.set_name(name.substr(colon + 1).c_str());
            auto ns = node.attribute("xmlns");
            if (!ns) ns = node.append_attribute("xmlns");
            ns.set_value(uri.c_str());
        }
        for (auto attr : node.attributes()) {
            const auto attribute = std::string(attr.name());
            const auto split = attribute.find(':');
            const bool reference =
                split != std::string::npos &&
                namespaces[attribute.substr(0, split)] ==
                    "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
            const auto found = relations.find(attr.value());
            if (found != relations.end() && ((rels && attribute == "Id") || reference))
                attr.set_value(found->second.c_str());
        }
        for (auto child : node.children()) normalize(child, namespaces, relations, rels);
    }

    OpenXLSX::XLZipArchive archive_;
};

void validatePosition(std::uint32_t row, std::uint16_t column) {
    if (row == 0 || row > kMaxRows || column == 0 || column > kMaxColumns) {
        throw std::out_of_range("Excel cell position is outside the worksheet limits");
    }
}

void validateRange(std::uint32_t row, std::uint16_t column, std::size_t rows, std::size_t columns) {
    validatePosition(row, column);
    if (rows > kMaxRows - row + 1 || columns > kMaxColumns - column + 1) {
        throw std::out_of_range("Excel range is outside the worksheet limits");
    }
}

OpenXLSX::XLCellReference parseAddress(const std::string& address) {
    // 严格校验后再交给底层解析，避免无效的大列号在 uint16_t 转换中截断。
    std::size_t index = 0;
    std::uint32_t column = 0;
    while (index < address.size() && address[index] >= 'A' && address[index] <= 'Z') {
        column = column * 26 + static_cast<unsigned>(address[index++] - 'A' + 1);
        if (column > kMaxColumns) {
            throw std::out_of_range("Excel column exceeds XFD");
        }
    }
    if (column == 0 || index == address.size() || address[index] < '1' || address[index] > '9') {
        throw std::invalid_argument("Expected an Excel A1 address, such as B2");
    }
    std::uint32_t row = 0;
    for (; index < address.size(); ++index) {
        if (address[index] < '0' || address[index] > '9') {
            throw std::invalid_argument("Expected an Excel A1 address, such as B2");
        }
        row = row * 10 + static_cast<unsigned>(address[index] - '0');
        if (row > kMaxRows) {
            throw std::out_of_range("Excel row exceeds 1048576");
        }
    }
    validatePosition(row, static_cast<std::uint16_t>(column));
    return OpenXLSX::XLCellReference(row, static_cast<std::uint16_t>(column));
}

std::string fileName(const std::filesystem::path& path) {
    auto extension = path.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (path.empty() || extension != ".xlsx") {
        throw std::invalid_argument("Excel file must have an .xlsx extension");
    }
    const auto name = path.u8string();
    if (name.find('\0') != std::string::npos) {
        throw std::invalid_argument("Excel file path contains a null character");
    }
    return name;
}

void validateValue(const ExcelValue& value) {
    if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number)) {
        throw std::invalid_argument("Excel numeric values must be finite");
    }
    if (const auto* string = std::get_if<std::string>(&value);
        string && string->find('\0') != std::string::npos) {
        throw std::invalid_argument("Excel text contains a null character");
    }
}

ExcelValue readValue(const OpenXLSX::XLCell& cell) {
    if (!cell) {
        return std::monostate{};
    }
    const auto& value = cell.value();
    switch (value.type()) {
        case OpenXLSX::XLValueType::Empty:
            return std::monostate{};
        case OpenXLSX::XLValueType::Boolean:
            return value.get<bool>();
        case OpenXLSX::XLValueType::Integer:
            return value.get<std::int64_t>();
        case OpenXLSX::XLValueType::Float:
            return value.get<double>();
        case OpenXLSX::XLValueType::String:
            return value.get<std::string>();
        case OpenXLSX::XLValueType::Error:
            throw std::domain_error("Excel cell contains an error value");
    }
    throw std::domain_error("Unsupported Excel cell value type");
}

void writeValue(OpenXLSX::XLCell cell, const ExcelValue& value) {
    cell.formula().clear();
    std::visit(
        [&cell](const auto& item) {
            if constexpr (std::is_same_v<std::decay_t<decltype(item)>, std::monostate>) {
                cell.value().clear();
            } else {
                cell.value() = item;
            }
        },
        value);
}

}  // namespace

struct Excel::Impl {
    OpenXLSX::XLDocument document{CompatibleArchive{}};
};

Excel::Excel() noexcept = default;
Excel::~Excel() = default;
Excel::Excel(Excel&& other) noexcept = default;

Excel& Excel::operator=(Excel&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
    }
    return *this;
}

Excel::Impl& Excel::requireOpen() const {
    if (!isOpen()) {
        throw std::logic_error("Excel workbook is not open");
    }
    return *impl_;
}

void Excel::create(const std::filesystem::path& path, bool overwrite) {
    if (isOpen()) {
        throw std::logic_error("Close the current Excel workbook before creating another");
    }
    const auto name = fileName(path);
    auto next = std::make_unique<Impl>();
    next->document.create(name, overwrite);
    impl_ = std::move(next);
}

void Excel::open(const std::filesystem::path& path) {
    if (isOpen()) {
        throw std::logic_error("Close the current Excel workbook before opening another");
    }
    const auto name = fileName(path);
    auto next = std::make_unique<Impl>();
    next->document.open(name);
    impl_ = std::move(next);
}

void Excel::save() { requireOpen().document.save(); }

void Excel::saveAs(const std::filesystem::path& path, bool overwrite) {
    requireOpen().document.saveAs(fileName(path), overwrite);
}

void Excel::close() noexcept { impl_.reset(); }

bool Excel::isOpen() const noexcept { return impl_ && impl_->document.isOpen(); }

std::vector<std::string> Excel::sheetNames() const {
    return requireOpen().document.workbook().worksheetNames();
}

bool Excel::hasSheet(const std::string& sheet) const {
    return requireOpen().document.workbook().worksheetExists(sheet);
}

void Excel::addSheet(const std::string& sheet) {
    requireOpen().document.workbook().addWorksheet(sheet);
}

void Excel::removeSheet(const std::string& sheet) {
    auto workbook = requireOpen().document.workbook();
    workbook.worksheet(sheet);  // 校验类型及存在性，不允许此接口删除图表页。
    if (workbook.worksheetNames().size() <= 1) {
        throw std::logic_error("Cannot remove the last Excel worksheet");
    }
    workbook.deleteSheet(sheet);
}

void Excel::renameSheet(const std::string& sheet, const std::string& newName) {
    auto workbook = requireOpen().document.workbook();
    if (sheet != newName && workbook.sheetExists(newName)) {
        throw std::invalid_argument("Excel sheet name already exists");
    }
    workbook.worksheet(sheet).setName(newName);
}

std::pair<std::uint32_t, std::uint16_t> Excel::usedSize(const std::string& sheet) const {
    auto worksheet = requireOpen().document.workbook().worksheet(sheet);
    return {worksheet.rowCount(), worksheet.columnCount()};
}

ExcelValue Excel::readCell(const std::string& sheet, std::uint32_t row,
                           std::uint16_t column) const {
    auto worksheet = requireOpen().document.workbook().worksheet(sheet);
    validatePosition(row, column);
    return readValue(worksheet.findCell(row, column));
}

ExcelValue Excel::readCell(const std::string& sheet, const std::string& address) const {
    const auto ref = parseAddress(address);
    return readCell(sheet, ref.row(), ref.column());
}

void Excel::writeCell(const std::string& sheet, std::uint32_t row, std::uint16_t column,
                      const ExcelValue& value) {
    auto worksheet = requireOpen().document.workbook().worksheet(sheet);
    validatePosition(row, column);
    validateValue(value);
    writeValue(worksheet.cell(row, column), value);
}

void Excel::writeCell(const std::string& sheet, const std::string& address,
                      const ExcelValue& value) {
    const auto ref = parseAddress(address);
    writeCell(sheet, ref.row(), ref.column(), value);
}

ExcelTable Excel::readRange(const std::string& sheet, std::uint32_t firstRow,
                            std::uint16_t firstColumn, std::uint32_t rowCount,
                            std::uint16_t columnCount) const {
    auto worksheet = requireOpen().document.workbook().worksheet(sheet);
    validateRange(firstRow, firstColumn, rowCount, columnCount);
    ExcelTable result;
    if (rowCount == 0 || columnCount == 0) {
        return result;
    }
    result.reserve(rowCount);
    for (std::uint32_t r = 0; r < rowCount; ++r) {
        ExcelRow row;
        row.reserve(columnCount);
        for (std::uint16_t c = 0; c < columnCount; ++c) {
            row.push_back(readValue(
                worksheet.findCell(firstRow + r, static_cast<std::uint16_t>(firstColumn + c))));
        }
        result.push_back(std::move(row));
    }
    return result;
}

void Excel::writeRange(const std::string& sheet, std::uint32_t firstRow, std::uint16_t firstColumn,
                       const ExcelTable& values) {
    auto worksheet = requireOpen().document.workbook().worksheet(sheet);
    const auto columns = values.empty() ? 0 : values.front().size();
    validateRange(firstRow, firstColumn, values.size(), columns);
    for (const auto& row : values) {
        if (row.size() != columns) {
            throw std::invalid_argument("Excel table rows must have equal lengths");
        }
        for (const auto& value : row) {
            validateValue(value);
        }
    }
    for (std::size_t r = 0; r < values.size(); ++r) {
        for (std::size_t c = 0; c < columns; ++c) {
            writeValue(worksheet.cell(firstRow + static_cast<std::uint32_t>(r),
                                      static_cast<std::uint16_t>(firstColumn + c)),
                       values[r][c]);
        }
    }
}

void Excel::writeFormula(const std::string& sheet, const std::string& address,
                         const std::string& formula) {
    auto worksheet = requireOpen().document.workbook().worksheet(sheet);
    const auto ref = parseAddress(address);
    const auto expression =
        !formula.empty() && formula.front() == '=' ? formula.substr(1) : formula;
    if (expression.empty() || expression.find('\0') != std::string::npos) {
        throw std::invalid_argument("Excel formula must be a nonempty expression without nulls");
    }
    auto cell = worksheet.cell(ref);
    cell.value().clear();
    cell.formula() = expression;
    cell.value().clear();  // 底层公式赋值会填入缓存零值；工具不计算公式。
}

std::string Excel::readFormula(const std::string& sheet, const std::string& address) const {
    auto worksheet = requireOpen().document.workbook().worksheet(sheet);
    auto cell = worksheet.findCell(parseAddress(address));
    return cell ? cell.formula().get() : std::string{};
}

}  // namespace ems::util

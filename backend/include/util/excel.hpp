#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ems::util {

/** @brief Excel 单元格值；monostate 表示空值，字符串使用 UTF-8。 */
using ExcelValue = std::variant<std::monostate, bool, std::int64_t, double, std::string>;
using ExcelRow = std::vector<ExcelValue>;
using ExcelTable = std::vector<ExcelRow>;

/**
 * @brief 基于 OpenXLSX 的 .xlsx 工具类，独占工作簿，支持移动，不支持复制。
 * @details 坐标从 1 开始；关闭和析构均不自动保存，须显式调用 save/saveAs。
 * 文件、工作表和底层库错误以异常报告；对象需要由调用方同步访问。
 * 公式仅存取表达式，不执行计算；readCell 读取已有的缓存值。
 */
class Excel {
   public:
    /** @brief 构造未打开文件的对象。 */
    Excel() noexcept;
    /** @brief 释放工作簿资源，不保存未提交的修改。 */
    ~Excel();
    /**
     * @brief 转移工作簿所有权，源对象变为未打开状态。
     * @param[in,out] other 被移动的对象。
     */
    Excel(Excel&& other) noexcept;
    /**
     * @brief 转移所有权，丢弃当前对象未保存的修改。
     * @param[in,out] other 被移动的对象；允许自移动。
     * @return 当前对象引用。
     */
    Excel& operator=(Excel&& other) noexcept;
    /**
     * @brief 禁止复制独占的工作簿。
     * @param[in] other 待复制对象。
     */
    Excel(const Excel& other) = delete;
    /**
     * @brief 禁止复制赋值。
     * @param[in] other 待复制对象。
     * @return 当前对象引用（接口不可调用）。
     */
    Excel& operator=(const Excel& other) = delete;

    /**
     * @brief 创建带默认 Sheet1 的文件；失败时对象保持关闭状态。
     * @param[in] path .xlsx 文件路径，父目录须已存在。
     * @param[in] overwrite 是否允许覆盖已有文件，默认拒绝覆盖。
     * @throws std::logic_error 对象已经打开文件。
     */
    void create(const std::filesystem::path& path, bool overwrite = false);
    /**
     * @brief 打开已有文件；失败时对象保持关闭状态。
     * @param[in] path .xlsx 文件路径。
     * @throws std::logic_error 对象已经打开文件。
     */
    void open(const std::filesystem::path& path);
    /** @brief 保存到当前路径；未打开时抛出 std::logic_error。 */
    void save();
    /**
     * @brief 另存文件，成功后当前路径切换到目标路径。
     * @param[in] path .xlsx 目标路径，父目录须已存在。
     * @param[in] overwrite 是否允许覆盖已有文件，默认拒绝覆盖。
     */
    void saveAs(const std::filesystem::path& path, bool overwrite = false);
    /** @brief 关闭文件并丢弃未保存的修改；可重复调用。 */
    void close() noexcept;
    /**
     * @brief 查询是否持有已打开的工作簿。
     * @return 已打开返回 true，否则返回 false。
     */
    [[nodiscard]] bool isOpen() const noexcept;

    /**
     * @brief 按工作簿顺序列出工作表（不包含图表页）。
     * @return 工作表名称列表。
     */
    [[nodiscard]] std::vector<std::string> sheetNames() const;
    /**
     * @brief 查询工作表是否存在。
     * @param[in] sheet UTF-8 工作表名称。
     * @return 存在返回 true，否则返回 false。
     */
    [[nodiscard]] bool hasSheet(const std::string& sheet) const;
    /**
     * @brief 添加工作表；名称无效或重复时抛出异常。
     * @param[in] sheet UTF-8 工作表名称。
     */
    void addSheet(const std::string& sheet);
    /**
     * @brief 删除工作表；不存在或仅剩一个工作表时抛出异常。
     * @param[in] sheet UTF-8 工作表名称。
     */
    void removeSheet(const std::string& sheet);
    /**
     * @brief 重命名工作表；无效或重复名称抛出异常。
     * @param[in] sheet 原工作表名称。
     * @param[in] newName 新工作表名称。
     */
    void renameSheet(const std::string& sheet, const std::string& newName);
    /**
     * @brief 查询已分配单元格的最大行列号（可能包含空值或格式）。
     * @param[in] sheet 工作表名称。
     * @return {最大行号, 最大列号}，空工作表的结果遵循 OpenXLSX。
     */
    [[nodiscard]] std::pair<std::uint32_t, std::uint16_t> usedSize(const std::string& sheet) const;

    /**
     * @brief 读取单元格；缺失或空单元格返回 monostate，不创建缺失单元格。
     * @param[in] sheet 工作表名称。
     * @param[in] row 行号，范围 [1, 1048576]。
     * @param[in] column 列号，范围 [1, 16384]。
     * @return 保留原始类型的单元格值。
     * @throws std::domain_error 单元格含 Excel 错误值。
     */
    [[nodiscard]] ExcelValue readCell(const std::string& sheet, std::uint32_t row,
                                      std::uint16_t column) const;
    /**
     * @brief 通过 A1 地址读取单元格，空值与错误语义同坐标版本。
     * @param[in] sheet 工作表名称。
     * @param[in] address A1 格式地址，例如 B2。
     * @return 保留原始类型的单元格值。
     */
    [[nodiscard]] ExcelValue readCell(const std::string& sheet, const std::string& address) const;
    /**
     * @brief 写入单元格，清除原公式；monostate 清除值并保留格式。
     * @param[in] sheet 工作表名称。
     * @param[in] row 从 1 开始的行号。
     * @param[in] column 从 1 开始的列号。
     * @param[in] value 单元格值；double 须为有限数。
     */
    void writeCell(const std::string& sheet, std::uint32_t row, std::uint16_t column,
                   const ExcelValue& value);
    /**
     * @brief 通过 A1 地址写入单元格，语义同坐标版本。
     * @param[in] sheet 工作表名称。
     * @param[in] address A1 格式地址。
     * @param[in] value 单元格值。
     */
    void writeCell(const std::string& sheet, const std::string& address, const ExcelValue& value);
    /**
     * @brief 读取矩形区域，包括空单元格；零行或零列返回空表。
     * @param[in] sheet 工作表名称。
     * @param[in] firstRow 起始行号，从 1 开始。
     * @param[in] firstColumn 起始列号，从 1 开始。
     * @param[in] rowCount 读取行数，整个区域须在 Excel 坐标范围内。
     * @param[in] columnCount 读取列数，整个区域须在 Excel 坐标范围内。
     * @return 按行组织的矩形数据。
     */
    [[nodiscard]] ExcelTable readRange(const std::string& sheet, std::uint32_t firstRow,
                                       std::uint16_t firstColumn, std::uint32_t rowCount,
                                       std::uint16_t columnCount) const;
    /**
     * @brief 写入矩形区域；预先校验形状、坐标与值，底层异常可能留下部分修改。
     * @param[in] sheet 工作表名称。
     * @param[in] firstRow 起始行号，从 1 开始。
     * @param[in] firstColumn 起始列号，从 1 开始。
     * @param[in] values 按行组织的数据，各行长度须相同；空表不写入。
     */
    void writeRange(const std::string& sheet, std::uint32_t firstRow, std::uint16_t firstColumn,
                    const ExcelTable& values);
    /**
     * @brief 写入公式，清除旧缓存值，不计算结果。
     * @param[in] sheet 工作表名称。
     * @param[in] address A1 格式地址。
     * @param[in] formula 公式表达式，可带开头的等号；空表达式无效。
     */
    void writeFormula(const std::string& sheet, const std::string& address,
                      const std::string& formula);
    /**
     * @brief 读取公式表达式。
     * @param[in] sheet 工作表名称。
     * @param[in] address A1 格式地址。
     * @return 不含开头等号的表达式；无公式或无单元格返回空字符串。
     */
    [[nodiscard]] std::string readFormula(const std::string& sheet,
                                          const std::string& address) const;

   private:
    struct Impl;
    /**
     * @brief 获取当前工作簿实现；未打开时抛出 std::logic_error。
     * @return 对象拥有的实现引用，调用方不得释放。
     */
    Impl& requireOpen() const;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ems::util

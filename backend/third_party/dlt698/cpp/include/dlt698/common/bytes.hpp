/**
 * @file bytes.hpp
 * @brief 字节视图、资源限制及带边界检查的二进制读写工具。
 */
#pragma once
#include <cstdint>
#include <dlt698/common/result.hpp>
#include <dlt698/export.hpp>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace dlt698 {
using Bytes = std::vector<std::uint8_t>;

/**
 * @brief 借用连续字节内存的只读视图，不拥有底层缓冲区。
 * @note 使用期间底层内存必须保持有效，且不能因容器扩容而移动。
 */
class ByteView {
   public:
    /** @brief 创建空字节视图。 */
    ByteView() = default;

    /**
     * @brief 借用指定内存中的字节。
     * @param[in] data 缓冲区首地址；仅在 size 为零时允许为空。
     * @param[in] size 可读取的字节数。
     * @throws std::invalid_argument 非空视图的首地址为空。
     */
    ByteView(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {
        if (!data && size) throw std::invalid_argument("null ByteView");
    }

    /**
     * @brief 借用字节容器的全部内容。
     * @param[in] data 生命周期覆盖视图使用过程的字节容器。
     */
    ByteView(const Bytes& data) noexcept : data_(data.data()), size_(data.size()) {}

    /** @brief 禁止借用临时容器，避免视图悬空。 */
    ByteView(Bytes&&) = delete;
    /** @brief 禁止借用 const 临时容器，避免视图悬空。 */
    ByteView(const Bytes&&) = delete;

    /**
     * @brief 获取借用内存的首地址。
     * @return 缓冲区指针；空视图可能返回 nullptr。
     */
    const std::uint8_t* data() const noexcept { return data_; }

    /**
     * @brief 获取视图长度。
     * @return 可读取的字节数。
     */
    std::size_t size() const noexcept { return size_; }

    /**
     * @brief 判断视图是否为空。
     * @return 长度为零时返回 true。
     */
    bool empty() const noexcept { return size_ == 0; }

    /**
     * @brief 按下标读取字节，不执行边界检查。
     * @param[in] i 从零开始的字节下标。
     * @pre i < size()。
     * @return 指定位置的字节。
     */
    std::uint8_t operator[](std::size_t i) const { return data_[i]; }

    /**
     * @brief 创建共享底层内存的子视图。
     * @param[in] offset 相对于当前视图的起始偏移。
     * @param[in] size 子视图字节数。
     * @return 不拥有内存的子视图。
     * @throws std::out_of_range 指定区间超出当前视图。
     */
    ByteView subview(std::size_t offset, std::size_t size) const {
        if (offset > size_ || size > size_ - offset) throw std::out_of_range("ByteView");
        return {data_ ? data_ + offset : nullptr, size};
    }

   private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
};

/// 编解码与流缓冲的资源上限。
struct Limits {
    std::size_t max_data_bytes = 1024 * 1024;  ///< 完整 Data/APDU 的输入和输出字节上限。
    std::size_t max_elements = 65536;      ///< 单个 Data 树的节点总数上限，以及 GET 属性数上限。
    std::size_t max_depth = 32;            ///< Data 嵌套深度上限，根节点深度为零。
    std::size_t max_frame_bytes = 16385;   ///< 14 位长度字段加起止符的完整帧字节上限。
    std::size_t max_stream_bytes = 32770;  ///< 流解析器缓存字节上限，须能容纳最大帧。
};

/**
 * @brief 组合读写工具使用的错误异常，由完整输入编解码接口转换为 Result。
 * @note 导出异常类型及其 RTTI，保证共享库与调用方之间可按此类型捕获异常。
 */
class DLT698_API DecodeFailure final {
   public:
    /**
     * @brief 保存解析或编码失败的诊断信息。
     * @param[in] error 包含错误类型、偏移和字段上下文的诊断信息。
     */
    explicit DecodeFailure(Error error) : error(std::move(error)) {}

    Error error;
};

/// 在借用字节视图上推进游标，读取失败时抛出 DecodeFailure。
class Reader {
   public:
    /**
     * @brief 创建从偏移零开始的读取器。
     * @param[in] input 生命周期覆盖读取过程的输入字节视图。
     */
    explicit Reader(ByteView input) : input_(input) {}

    /**
     * @brief 获取当前读取游标。
     * @return 相对于输入起点的字节偏移。
     */
    std::size_t position() const noexcept { return position_; }

    /**
     * @brief 获取尚未消费的输入长度。
     * @return 剩余字节数。
     */
    std::size_t remaining() const noexcept { return input_.size() - position_; }

    /**
     * @brief 检查剩余输入是否足够，不推进游标。
     * @param[in] n 所需字节数。
     * @param[in] field 失败时写入错误上下文的字段名称。
     * @throws DecodeFailure 输入不足，错误码为 need_more_data。
     */
    void require(std::size_t n, const char* field) const {
        if (n > remaining()) throw DecodeFailure({ErrorCode::need_more_data, position_, field});
    }

    /**
     * @brief 读取一个字节并推进游标。
     * @param[in] field 失败时使用的字段名称。
     * @return 无符号字节值。
     * @throws DecodeFailure 输入不足。
     */
    std::uint8_t u8(const char* field = "byte") {
        require(1, field);
        return input_[position_++];
    }

    /**
     * @brief 按大端序读取无符号整数并推进游标。
     * @param[in] n 整数字节宽度，允许 0 至 8。
     * @param[in] field 失败时使用的字段名称。
     * @return 读取的整数；宽度为零时返回零。
     * @throws DecodeFailure 宽度超过 8 或输入不足。
     */
    std::uint64_t be(std::size_t n, const char* field) {
        if (n > 8) throw DecodeFailure({ErrorCode::invalid_length, position_, field});
        require(n, field);
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < n; ++i) value = (value << 8) | input_[position_++];
        return value;
    }

    /**
     * @brief 复制连续字节并推进游标。
     * @param[in] n 需要读取的字节数。
     * @param[in] field 失败时使用的字段名称。
     * @return 拥有独立内存的字节容器。
     * @throws DecodeFailure 输入不足。
     */
    Bytes bytes(std::size_t n, const char* field) {
        require(n, field);
        Bytes value;
        if (n) value.assign(input_.data() + position_, input_.data() + position_ + n);
        position_ += n;
        return value;
    }

    /**
     * @brief 检查完整输入是否已被消费。
     * @throws DecodeFailure 存在剩余字节，错误码为 trailing_data。
     */
    void finish() const {
        if (remaining())
            throw DecodeFailure({ErrorCode::trailing_data, position_, "trailing bytes"});
    }

   private:
    ByteView input_;
    std::size_t position_ = 0;
};

/// 拥有输出缓冲区的二进制写入器，始终检查输出字节上限。
class Writer {
   public:
    /**
     * @brief 创建空写入器。
     * @param[in] limit 输出缓冲区允许的最大字节数。
     */
    explicit Writer(std::size_t limit) : limit_(limit) {}

    /**
     * @brief 追加一个字节。
     * @param[in] value 待写入的字节值。
     * @throws DecodeFailure 输出超过资源上限。
     */
    void u8(std::uint8_t value) {
        reserve(1);
        data_.push_back(value);
    }

    /**
     * @brief 将整数的低 n 个字节按大端序追加。
     * @param[in] value 待编码整数，高于指定宽度的位被截断。
     * @param[in] n 输出宽度，允许 0 至 8 字节。
     * @throws DecodeFailure 宽度超过 8 或输出超过资源上限。
     */
    void be(std::uint64_t value, std::size_t n) {
        if (n > 8) throw DecodeFailure({ErrorCode::invalid_length, data_.size(), "numeric width"});
        reserve(n);
        for (std::size_t i = n; i; --i)
            data_.push_back(static_cast<std::uint8_t>(value >> ((i - 1) * 8)));
    }

    /**
     * @brief 复制并追加字节视图内容。
     * @param[in] value 在调用期间保持有效的字节视图。
     * @throws DecodeFailure 输出超过资源上限。
     */
    void bytes(ByteView value) {
        reserve(value.size());
        if (!value.empty()) data_.insert(data_.end(), value.data(), value.data() + value.size());
    }

    /**
     * @brief 移出输出缓冲区，通常作为本次编码的最后一步。
     * @return 拥有编码结果内存的字节容器。
     */
    Bytes take() { return std::move(data_); }

    /**
     * @brief 获取已编码长度。
     * @return 当前输出缓冲区的字节数。
     */
    std::size_t size() const noexcept { return data_.size(); }

   private:
    /**
     * @brief 检查追加长度是否超限，不分配内存。
     * @param[in] n 本次准备追加的字节数。
     * @throws DecodeFailure 输出超过资源上限。
     */
    void reserve(std::size_t n) const {
        if (n > limit_ - data_.size())
            throw DecodeFailure({ErrorCode::resource_limit, data_.size(), "output limit"});
    }

    Bytes data_;
    std::size_t limit_;
};

/**
 * @brief 将十六进制文本转换为字节序列。
 * @param[in] text 大小写均可的十六进制字符，完整字节之间允许空白，不接受 0x 前缀。
 * @return 字节序列；非法字符或半字节间空白返回 invalid_value，奇数位返回 invalid_length。
 * @note 失败时 Error::offset 是文本中的字符偏移。
 */
DLT698_API Result<Bytes> from_hex(std::string_view text);
/**
 * @brief 将字节序列转换为大写十六进制文本。
 * @param[in] bytes 待转换字节视图。
 * @return 字节之间用单个空格分隔的文本；空输入返回空字符串。
 */
DLT698_API std::string to_hex(ByteView bytes);
}  // namespace dlt698

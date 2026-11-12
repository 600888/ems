/**
 * @file frame.hpp
 * @brief 链路帧模型、HCS/FCS 校验及增量流解析。
 */
#pragma once
#include <dlt698/common/bytes.hpp>
#include <variant>

namespace dlt698::protocol::link {
/// 服务器地址类型，对应地址描述字节的高两位。
enum class AddressType : std::uint8_t { single = 0, wildcard = 1, group = 2, broadcast = 3 };

/// 服务器地址，编码时长度须为 1 至 16 字节，逻辑地址须为 0 至 3。
struct ServerAddress {
    AddressType type = AddressType::single;
    std::uint8_t logical = 0;
    Bytes bytes = {0};  ///< 按线序保存，地址的低有效字节在前。

    /**
     * @brief 比较地址类型、逻辑地址及地址字节。
     * @param[in] a 左侧地址。
     * @param[in] b 右侧地址。
     * @return 所有字段一致时返回 true。
     */
    friend bool operator==(const ServerAddress& a, const ServerAddress& b) {
        return a.type == b.type && a.logical == b.logical && a.bytes == b.bytes;
    }
};

/// 完整链路帧的逻辑字段，不包含前导字节。
struct Frame {
    std::uint8_t control = 0x43;
    ServerAddress server;
    std::uint8_t client = 0;
    Bytes payload;  ///< 未扰码的链路用户数据，可能包含分帧头；本层不负责重组。

    /**
     * @brief 比较控制字、地址和未扰码的用户数据。
     * @param[in] a 左侧帧。
     * @param[in] b 右侧帧。
     * @return 所有逻辑字段一致时返回 true。
     */
    friend bool operator==(const Frame& a, const Frame& b) {
        return a.control == b.control && a.server == b.server && a.client == b.client &&
               a.payload == b.payload;
    }
};

/**
 * @brief 计算 HCS/FCS 使用的 CRC-16，初值和最终异或值均为 0xFFFF。
 * @param[in] bytes 需要参与校验的连续字节。
 * @return 使用反射多项式 0x8408 计算的校验值，线上以低字节在前发送。
 */
DLT698_API std::uint16_t crc16(ByteView bytes) noexcept;
/**
 * @brief 将逻辑帧编码为包含起止符、HCS 和 FCS 的完整链路帧。
 * @param[in] frame 待编码帧，payload 应为未扰码的数据。
 * @param[in] limits 完整帧字节上限，同时受 14 位协议长度字段约束。
 * @return 完整帧字节序列，或控制字/地址非法、资源超限等错误。
 * @note 控制字 SC 位启用时，本接口对用户数据执行加 0x33 扰码。
 */
DLT698_API Result<Bytes> encode_frame(const Frame& frame, const Limits& limits = {});
/**
 * @brief 校验并解码恰好一个完整链路帧。
 * @param[in] bytes 从 0x68 到 0x16 的完整帧，不允许前导或尾随字节。
 * @param[in] limits 完整帧字节上限。
 * @return 拥有独立内存、已去扰码的帧，或长度、校验及字段错误。
 */
DLT698_API Result<Frame> decode_frame(ByteView bytes, const Limits& limits = {});
/// 流解析事件：有效帧或坏帧/资源限制的错误诊断。
using StreamEvent = std::variant<Frame, Error>;

/**
 * @brief 从任意字节块中增量提取帧，保留半帧并跳过前导和噪声。
 * @note 非线程安全，调用方应串行使用；合法头部后的半帧超时由调用方 reset。
 */
class FrameStreamDecoder {
   public:
    /**
     * @brief 创建空的流解析器。
     * @param[in] limits 帧长度须在 12 至 16385 字节之间，流缓存上限不得小于帧上限。
     * @throws std::invalid_argument 限制配置不满足上述条件。
     */
    DLT698_API explicit FrameStreamDecoder(Limits limits = {});
    /** @brief 释放缓存的未完成帧。 */
    DLT698_API ~FrameStreamDecoder();
    /** @brief 禁止复制带有解析进度的流解析器。 */
    FrameStreamDecoder(const FrameStreamDecoder&) = delete;
    /** @brief 禁止通过复制赋值共享解析进度。 */
    FrameStreamDecoder& operator=(const FrameStreamDecoder&) = delete;
    /** @brief 移入另一个解析器的资源限制及缓存。 */
    DLT698_API FrameStreamDecoder(FrameStreamDecoder&&) noexcept;
    /**
     * @brief 通过移动赋值接管另一个解析器的资源限制及缓存。
     * @return 当前解析器的引用。
     */
    DLT698_API FrameStreamDecoder& operator=(FrameStreamDecoder&&) noexcept;
    /**
     * @brief 输入一个字节块并提取当前可确定的解析事件。
     * @param[in] bytes 本次收到的字节，调用期间复制到内部缓存。
     * @return 按解析顺序排列的帧和错误；仅收到半帧时可能为空。
     * @note 坏帧产生错误后逐字节寻找下一起始符；普通噪声直接跳过。
     */
    DLT698_API std::vector<StreamEvent> feed(ByteView bytes);

    /** @brief 丢弃尚未完成的帧及噪声缓存，保留资源限制。 */
    void reset() noexcept { buffer_.clear(); }

    /**
     * @brief 查询等待后续字节的缓存长度。
     * @return 当前内部缓存的字节数。
     */
    std::size_t buffered_size() const noexcept { return buffer_.size(); }

   private:
    /**
     * @brief 解析缓存中的完整帧，并移除已消费的字节。
     * @param[in,out] events 追加有效帧和坏帧诊断的事件容器。
     */
    void drain(std::vector<StreamEvent>& events);
    Limits limits_;
    Bytes buffer_;
};
}  // namespace dlt698::protocol::link

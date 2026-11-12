/** @file fragment.hpp
 * @brief 独立于 APDU 服务的有界链路分帧状态机。
 */
#pragma once
#include <dlt698/protocol/link/frame.hpp>
#include <optional>

namespace dlt698::protocol::link {
enum class FragmentType : std::uint8_t { first = 0, last = 1, acknowledgement = 2, middle = 3 };

struct Fragment {
    FragmentType type = FragmentType::first;
    std::uint16_t sequence = 0;
    Bytes data;
};

/** @brief 解码小端两字节分帧格式域和片段。
 * @param[in] payload 已解除 SC 扰码的链路用户数据。
 * @return 有类型的片段，或保留位、类型、长度错误。
 */
DLT698_API Result<Fragment> decode_fragment(ByteView payload);
/** @brief 编码分帧格式域，确认帧不得包含数据。
 * @param[in] fragment 序号 0 至 4095 的片段，数据帧不得为空。
 * @return 用户数据字节或字段错误。
 */
DLT698_API Result<Bytes> encode_fragment(const Fragment& fragment);

class LinkFragmenter {
   public:
    /** @brief 保存拥有内存的 APDU，并建立从零开始的逐帧发送过程。
     * @param[in] apdu 至少分为两片的完整 APDU。
     * @param[in] fragment_bytes 每片数据字节上限，不含两字节格式域，须大于零。
     * @param[in] limit APDU 拥有内存的上限。
     * @throws std::invalid_argument 参数非法或 APDU 超限。
     */
    DLT698_API LinkFragmenter(Bytes apdu, std::size_t fragment_bytes, std::size_t limit);
    /** @brief 获取当前片段，重复调用不前进，可用于超时重发。
     * @return 拥有片段内存的值。
     */
    DLT698_API Fragment current() const;
    /** @brief 用最近正确序号推进一次发送，不接受过期确认。
     * @param[in] sequence 对端确认的序号。
     * @return 成功或序号不匹配错误。
     */
    DLT698_API Result<void> acknowledge(std::uint16_t sequence);

   private:
    Bytes apdu_;
    std::size_t chunk_, offset_ = 0;
    std::uint16_t sequence_ = 0;
};

struct Reassembly {
    std::optional<std::uint16_t> acknowledge;
    std::optional<Bytes> apdu;
    bool duplicate = false;
};

class LinkReassembler {
   public:
    /** @brief 建立单对端、单条重组通道，不创建定时器。
     * @param[in] limit 完整 APDU 字节上限，须大于零。
     * @throws std::invalid_argument 预算为零。
     * @note 调用方负责地址、方向、超时检查及调用 reset 回收未完成缓冲。
     */
    DLT698_API explicit LinkReassembler(std::size_t limit);
    /** @brief 接受按序片段，重复片段不重复提交 APDU。
     * @param[in] fragment 数据片段，不接受确认帧；起始片段须为序号零。
     * @return 需要确认的非末片序号、完整 APDU 或错误；乱序不污染已有缓冲。
     * @note 按标准图 16/17，末片直接交付应用，不等待链路确认。
     */
    DLT698_API Result<Reassembly> accept(const Fragment& fragment);
    /** @brief 终止重组，并清除重复片段记忆。 */
    DLT698_API void reset();

    /** @brief 查询是否仍等待后续片段。
     * @return 已收到起始片且尚未完成时为 true。
     */
    bool active() const noexcept { return active_; }

   private:
    std::size_t limit_;
    Bytes data_;
    std::optional<Fragment> previous_;
    bool active_ = false;
};
}  // namespace dlt698::protocol::link

/** @file security_detail.hpp
 * @brief 不带 Data 标签的安全与端口字段编解码。
 */
#pragma once
#include <dlt698/codec/data_codec.hpp>

namespace dlt698::codec::fields {
/** @brief 读取 A-XDR 长度及原始字节，返回拥有型缓冲区。
 * @param[in,out] r 输入读取器。
 * @param[in] l 字节资源上限。
 * @return 复制的原始字段。
 * @throws DecodeFailure 长度非法、超限或输入不足。
 */
inline Bytes octets(Reader& r, const Limits& l) {
    return r.bytes(read_length(r, l.max_data_bytes), "octets");
}

/** @brief 写入 A-XDR 长度及原始字节。
 * @param[in,out] w 输出写入器。
 * @param[in] v 仅调用期间借用的字节字段。
 * @throws DecodeFailure 输出超限。
 */
inline void octets(Writer& w, ByteView v) {
    write_length(w, v.size());
    w.bytes(v);
}

/** @brief 读取并检查 TSA 描述字节与地址长度。
 * @param[in,out] r 输入读取器。
 * @param[in] l 字节上限。
 * @return 拥有型地址，业务地址类型由调用层校验。
 * @throws DecodeFailure TSA 长度非法、输入不足或超限。
 */
inline model::Tsa tsa(Reader& r, const Limits& l) {
    auto v = octets(r, l);
    if (v.size() < 2 || v.size() > 17 || (v[0] & 15) + 2u != v.size())
        throw DecodeFailure({ErrorCode::invalid_value, r.position(), "TSA length"});
    return {std::move(v)};
}

/** @brief 校验并写入 TSA 字段。
 * @param[in,out] w 输出写入器。
 * @param[in] v 含描述字节的地址。
 * @throws DecodeFailure TSA 长度非法或输出超限。
 */
inline void tsa(Writer& w, const model::Tsa& v) {
    if (v.value.size() < 2 || v.value.size() > 17 || (v.value[0] & 15) + 2u != v.value.size())
        throw DecodeFailure({ErrorCode::invalid_value, w.size(), "TSA length"});
    octets(w, v.value);
}

/** @brief 读取安全标识及附加数据。
 * @param[in,out] r 输入读取器。
 * @param[in] l 附加数据上限。
 * @return 拥有型 SID。
 * @throws DecodeFailure 输入不足、长度非法或超限。
 */
inline model::Sid sid(Reader& r, const Limits& l) {
    const auto id = static_cast<std::uint32_t>(r.be(4, "SID"));
    return {id, octets(r, l)};
}

/** @brief 写入安全标识及附加数据。
 * @param[in,out] w 输出写入器。
 * @param[in] v 安全标识，不解释厂商私有附加数据。
 * @throws DecodeFailure 输出超限。
 */
inline void sid(Writer& w, const model::Sid& v) {
    w.be(v.identifier, 4);
    octets(w, v.additional);
}

/** @brief 读取 SID 和 MAC，不执行密码验证。
 * @param[in,out] r 输入读取器。
 * @param[in] l 字节上限。
 * @return 拥有型 SID_MAC。
 * @throws DecodeFailure 输入不足、长度非法或超限。
 */
inline model::SidMac sid_mac(Reader& r, const Limits& l) {
    auto id = sid(r, l);
    auto mac = octets(r, l);
    return {std::move(id), model::Mac{std::move(mac)}};
}

/** @brief 写入 SID 和 MAC。
 * @param[in,out] w 输出写入器。
 * @param[in] v 安全字段。
 * @throws DecodeFailure 输出超限。
 */
inline void sid_mac(Writer& w, const model::SidMac& v) {
    sid(w, v.sid);
    octets(w, v.mac.value);
}

/** @brief 校验标准通信控制枚举范围。
 * @param[in] v 端口配置。
 * @param[in] offset 错误报告偏移。
 * @throws DecodeFailure 枚举或数据位/停止位非法。
 */
inline void validate(const model::Comdcb& v, std::size_t offset) {
    if ((v.baud > 10 && v.baud != 255) || v.parity > 2 || v.data_bits < 5 || v.data_bits > 8 ||
        v.stop_bits < 1 || v.stop_bits > 2 || v.flow_control > 2)
        throw DecodeFailure({ErrorCode::invalid_value, offset, "COMDCB"});
}

/** @brief 读取并校验五字节 COMDCB。
 * @param[in,out] r 输入读取器。
 * @return 端口配置。
 * @throws DecodeFailure 输入不足或字段非法。
 */
inline model::Comdcb comdcb(Reader& r) {
    model::Comdcb v{r.u8("baud"), r.u8("parity"), r.u8("data bits"), r.u8("stop bits"),
                    r.u8("flow control")};
    validate(v, r.position() - 5);
    return v;
}

/** @brief 校验并写入五字节 COMDCB。
 * @param[in,out] w 输出写入器。
 * @param[in] v 端口配置。
 * @throws DecodeFailure 字段非法或输出超限。
 */
inline void comdcb(Writer& w, const model::Comdcb& v) {
    validate(v, w.size());
    w.u8(v.baud);
    w.u8(v.parity);
    w.u8(v.data_bits);
    w.u8(v.stop_bits);
    w.u8(v.flow_control);
}
}  // namespace dlt698::codec::fields

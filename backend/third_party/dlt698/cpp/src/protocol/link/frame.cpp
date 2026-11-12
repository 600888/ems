#include <algorithm>
#include <dlt698/protocol/link/frame.hpp>

namespace dlt698::protocol::link {
namespace {
std::uint16_t little(ByteView bytes, std::size_t i) {
    return static_cast<std::uint16_t>(bytes[i] | (static_cast<unsigned>(bytes[i + 1]) << 8));
}

void append16(Bytes& bytes, std::uint16_t v) {
    bytes.push_back(static_cast<std::uint8_t>(v));
    bytes.push_back(static_cast<std::uint8_t>(v >> 8));
}

bool valid_control(std::uint8_t control) {
    // 控制字保留位必须为零；当前仅接受链路管理和用户数据功能码。
    const auto function = control & 7;
    return !(control & 0x10) && (function == 1 || function == 3);
}
}  // namespace

std::uint16_t crc16(ByteView bytes) noexcept {
    // 按低位优先的反射算法计算；0x8408 对应多项式 0x1021，结束时取反。
    std::uint16_t crc = 0xffff;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = static_cast<std::uint16_t>((crc >> 1) ^ ((crc & 1) ? 0x8408 : 0));
    }
    return static_cast<std::uint16_t>(crc ^ 0xffff);
}

Result<Bytes> encode_frame(const Frame& f, const Limits& limits) {
    if (!valid_control(f.control))
        return Error{ErrorCode::invalid_value, 3, "control bits/function"};
    if (f.server.bytes.empty() || f.server.bytes.size() > 16 || f.server.logical > 3 ||
        static_cast<unsigned>(f.server.type) > 3)
        return Error{ErrorCode::invalid_value, 4, "server address"};
    if (f.server.type == AddressType::broadcast && f.server.bytes != Bytes{0xaa})
        return Error{ErrorCode::invalid_value, 5, "broadcast address"};
    // 固定开销包括起止符、长度、控制字、地址描述、客户机地址和两段 CRC。
    // 协议长度不包含起止符，因此完整帧的理论最大长度为 0x3FFF + 2。
    const auto overhead = 11 + f.server.bytes.size();
    if (limits.max_frame_bytes < overhead || f.payload.size() > limits.max_frame_bytes - overhead ||
        f.payload.size() > 16385 - overhead)
        return Error{ErrorCode::resource_limit, 1, "frame length"};
    const auto total = overhead + f.payload.size();
    Bytes bytes;
    bytes.reserve(total);
    bytes.push_back(0x68);
    append16(bytes, static_cast<std::uint16_t>(total - 2));
    bytes.push_back(f.control);
    bytes.push_back(static_cast<std::uint8_t>((static_cast<unsigned>(f.server.type) << 6) |
                                              (f.server.logical << 4) |
                                              (f.server.bytes.size() - 1)));
    bytes.insert(bytes.end(), f.server.bytes.begin(), f.server.bytes.end());
    bytes.push_back(f.client);
    // HCS 覆盖长度字段至客户机地址；FCS 覆盖长度字段至线上用户数据（含 HCS）。
    // 两者均不包含起始符，FCS 必须在完成扰码之后计算。
    append16(bytes, crc16(ByteView(bytes).subview(1, bytes.size() - 1)));
    for (auto b : f.payload)
        bytes.push_back(static_cast<std::uint8_t>(b + ((f.control & 8) ? 0x33 : 0)));
    append16(bytes, crc16(ByteView(bytes).subview(1, bytes.size() - 1)));
    bytes.push_back(0x16);
    return bytes;
}

Result<Frame> decode_frame(ByteView bytes, const Limits& limits) {
    if (bytes.size() > limits.max_frame_bytes)
        return Error{ErrorCode::resource_limit, 0, "frame input limit"};
    if (bytes.size() < 3) return Error{ErrorCode::need_more_data, bytes.size(), "frame length"};
    if (bytes[0] != 0x68) return Error{ErrorCode::invalid_value, 0, "start byte"};
    const auto length = little(bytes, 1);
    if ((length & 0xc000) || length < 10)
        return Error{ErrorCode::invalid_length, 1, "2017 length field"};
    const auto total = static_cast<std::size_t>(length) + 2;
    if (total > limits.max_frame_bytes)
        return Error{ErrorCode::resource_limit, 1, "frame declared length"};
    if (bytes.size() < total)
        return Error{ErrorCode::need_more_data, bytes.size(), "incomplete frame"};
    if (bytes.size() > total) return Error{ErrorCode::trailing_data, total, "frame trailing bytes"};
    if (bytes[total - 1] != 0x16) return Error{ErrorCode::invalid_value, total - 1, "end byte"};
    const auto address_count = static_cast<std::size_t>((bytes[4] & 15) + 1);
    const auto hcs_offset = 6 + address_count;
    // 地址长度来自不可信输入，先确认 HCS、FCS 和结束符都位于帧内，再访问校验字段。
    if (hcs_offset + 5 > total) return Error{ErrorCode::invalid_length, 4, "address exceeds frame"};
    if (crc16(bytes.subview(1, hcs_offset - 1)) != little(bytes, hcs_offset))
        return Error{ErrorCode::checksum_header, hcs_offset, "HCS"};
    if (crc16(bytes.subview(1, total - 4)) != little(bytes, total - 3))
        return Error{ErrorCode::checksum_frame, total - 3, "FCS"};
    if (!valid_control(bytes[3]))
        return Error{ErrorCode::invalid_value, 3, "control bits/function"};
    Frame frame;
    frame.control = bytes[3];
    frame.server.type = static_cast<AddressType>(bytes[4] >> 6);
    frame.server.logical = (bytes[4] >> 4) & 3;
    frame.server.bytes.assign(bytes.data() + 5, bytes.data() + 5 + address_count);
    frame.client = bytes[5 + address_count];
    if (frame.server.type == AddressType::broadcast && frame.server.bytes != Bytes{0xaa})
        return Error{ErrorCode::invalid_value, 5, "broadcast address"};
    frame.payload.assign(bytes.data() + hcs_offset + 2, bytes.data() + total - 3);
    // 校验针对线上扰码字节；通过校验后再减 0x33，向上层交付逻辑用户数据。
    if (frame.control & 8)
        for (auto& b : frame.payload) b = static_cast<std::uint8_t>(b - 0x33);
    return frame;
}

FrameStreamDecoder::FrameStreamDecoder(Limits limits) : limits_(limits) {
    if (limits_.max_frame_bytes < 12 || limits_.max_frame_bytes > 16385 ||
        limits_.max_stream_bytes < limits_.max_frame_bytes)
        throw std::invalid_argument("stream limits must hold a maximum frame");
}

FrameStreamDecoder::~FrameStreamDecoder() = default;
FrameStreamDecoder::FrameStreamDecoder(FrameStreamDecoder&&) noexcept = default;
FrameStreamDecoder& FrameStreamDecoder::operator=(FrameStreamDecoder&&) noexcept = default;

void FrameStreamDecoder::drain(std::vector<StreamEvent>& events) {
    std::size_t consumed = 0;
    while (consumed < buffer_.size()) {
        if (buffer_[consumed] != 0x68) {
            ++consumed;
            continue;
        }
        auto candidate = ByteView(buffer_).subview(consumed, buffer_.size() - consumed);
        if (candidate.size() < 3) break;
        const auto length = little(candidate, 1);
        const auto total = static_cast<std::size_t>(length) + 2;
        if ((length & 0xc000) || length < 10 || total > limits_.max_frame_bytes) {
            events.emplace_back(Error{ErrorCode::invalid_length, 1, "stream length"});
            ++consumed;
            continue;
        }
        // 头部一旦完整就检查 HCS，避免坏头部伪造大长度而阻塞后续有效帧。
        if (candidate.size() >= 5) {
            const auto hcs = 6 + static_cast<std::size_t>((candidate[4] & 15) + 1);
            if (hcs + 5 > total) {
                events.emplace_back(Error{ErrorCode::invalid_length, 4, "stream address"});
                ++consumed;
                continue;
            }
            if (candidate.size() >= hcs + 2 &&
                crc16(candidate.subview(1, hcs - 1)) != little(candidate, hcs)) {
                events.emplace_back(Error{ErrorCode::checksum_header, hcs, "stream HCS"});
                ++consumed;
                continue;
            }
        }
        // 头部合法但正文未收齐时保留半帧；是否超时由会话层决定。
        if (candidate.size() < total) break;
        auto result = decode_frame(candidate.subview(0, total), limits_);
        if (result) {
            events.emplace_back(std::move(result).value());
            consumed += total;
        } else {
            events.emplace_back(result.error());
            // 坏帧只跳过一个字节，不能按其声明长度跳过，以免漏掉后续真实起始符。
            ++consumed;
        }
    }
    if (consumed)
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(consumed));
}

std::vector<StreamEvent> FrameStreamDecoder::feed(ByteView input) {
    std::vector<StreamEvent> events;
    std::size_t offset = 0;
    while (offset < input.size()) {
        // 分批填充并持续消费，避免一个很大的输入块使内部缓存突破资源上限。
        drain(events);
        const auto available = limits_.max_stream_bytes - buffer_.size();
        if (!available) {
            events.emplace_back(Error{ErrorCode::resource_limit, 0, "stream buffer"});
            reset();
            continue;
        }
        const auto count = std::min(available, input.size() - offset);
        buffer_.insert(buffer_.end(), input.data() + offset, input.data() + offset + count);
        offset += count;
    }
    drain(events);
    return events;
}
}  // namespace dlt698::protocol::link

#include <algorithm>
#include <dlt698/protocol/link/fragment.hpp>

namespace dlt698::protocol::link {
Result<Fragment> decode_fragment(ByteView bytes) {
    if (bytes.size() < 2) return Error{ErrorCode::need_more_data, 0, "fragment header"};
    const auto word = static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
    if (word & 0x3000) return Error{ErrorCode::invalid_value, 0, "fragment reserved bits"};
    Fragment f{static_cast<FragmentType>(word >> 14), static_cast<std::uint16_t>(word & 0xfff), {}};
    if ((f.type == FragmentType::acknowledgement && bytes.size() != 2) ||
        (f.type != FragmentType::acknowledgement && bytes.size() == 2))
        return Error{ErrorCode::invalid_length, 2, "fragment data"};
    f.data.assign(bytes.data() + 2, bytes.data() + bytes.size());
    return f;
}

Result<Bytes> encode_fragment(const Fragment& f) {
    if (f.sequence > 4095 || static_cast<unsigned>(f.type) > 3)
        return Error{ErrorCode::invalid_value, 0, "fragment fields"};
    if ((f.type == FragmentType::acknowledgement) != f.data.empty())
        return Error{ErrorCode::invalid_length, 2, "fragment data"};
    const auto word =
        static_cast<std::uint16_t>(f.sequence | (static_cast<unsigned>(f.type) << 14));
    Bytes bytes{static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8)};
    bytes.insert(bytes.end(), f.data.begin(), f.data.end());
    return bytes;
}

LinkFragmenter::LinkFragmenter(Bytes apdu, std::size_t chunk, std::size_t limit)
    : apdu_(std::move(apdu)), chunk_(chunk) {
    if (!chunk || apdu_.size() <= chunk || apdu_.size() > limit)
        throw std::invalid_argument("fragmenter budget");
}

Fragment LinkFragmenter::current() const {
    const auto n = std::min(chunk_, apdu_.size() - offset_);
    return {offset_ == 0                  ? FragmentType::first
            : offset_ + n == apdu_.size() ? FragmentType::last
                                          : FragmentType::middle,
            sequence_, Bytes(apdu_.begin() + offset_, apdu_.begin() + offset_ + n)};
}

Result<void> LinkFragmenter::acknowledge(std::uint16_t sequence) {
    if (sequence != sequence_ || current().type == FragmentType::last)
        return Error{ErrorCode::invalid_value, 0, "fragment ACK sequence"};
    offset_ += chunk_;
    // 序号域仅 12 位；重组和发送进度由字节数决定，不能用序号判断总长度。
    sequence_ = static_cast<std::uint16_t>((sequence_ + 1) & 0xfff);
    return {};
}

LinkReassembler::LinkReassembler(std::size_t limit) : limit_(limit) {
    if (!limit) throw std::invalid_argument("reassembly limit");
}

void LinkReassembler::reset() {
    data_.clear();
    previous_.reset();
    active_ = false;
}

Result<Reassembly> LinkReassembler::accept(const Fragment& f) {
    if (f.sequence > 4095 || static_cast<unsigned>(f.type) > 3 ||
        f.type == FragmentType::acknowledgement || f.data.empty())
        return Error{ErrorCode::invalid_value, 0, "reassembly fragment"};
    if (previous_ && f.sequence == previous_->sequence && f.type == previous_->type &&
        f.data == previous_->data)
        return Reassembly{
            f.type == FragmentType::last ? std::optional<std::uint16_t>{} : f.sequence, {}, true};
    if (f.type == FragmentType::first) {
        if (f.sequence || active_)
            return Error{ErrorCode::invalid_value, 0, "unexpected first fragment"};
        data_.clear();
        active_ = true;
    } else if (!active_ || !previous_ || f.sequence != ((previous_->sequence + 1) & 0xfff))
        return Error{ErrorCode::invalid_value, 0, "out of order fragment"};
    if (f.data.size() > limit_ - data_.size()) {
        reset();
        return Error{ErrorCode::resource_limit, 0, "reassembled APDU bytes"};
    }
    data_.insert(data_.end(), f.data.begin(), f.data.end());
    previous_ = f;
    if (f.type == FragmentType::last) {
        active_ = false;
        auto apdu = std::move(data_);
        data_.clear();
        return Reassembly{{}, std::move(apdu), false};
    }
    return Reassembly{f.sequence, {}, false};
}
}  // namespace dlt698::protocol::link

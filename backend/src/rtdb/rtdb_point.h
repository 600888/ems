#ifndef EMS_RTDB_POINT_H
#define EMS_RTDB_POINT_H

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <map>

namespace ems {
namespace rtdb {

enum class DecodeCode {
    UINT16,
    INT16,
    UINT32,
    INT32,
    FLOAT,
    DOUBLE,
    UINT8,
    INT8,
    BOOL,
    BIT
};

enum class PointType {
    YC,
    YX,
    YK,
    YT
};

enum class PointQuality {
    Good = 0,
    Invalid = 1,
    Reserved = 2,
    Error = 3
};

enum class YKStatus {
    Idle = 0,
    Pending = 1,
    Executing = 2,
    Success = 3,
    Failed = 4
};

class BasePoint {
public:
    virtual ~BasePoint() = default;

    virtual std::string code() const = 0;
    virtual std::string name() const = 0;
    virtual PointType type() const = 0;
    virtual uint64_t timestamp() const = 0;
    virtual PointQuality quality() const = 0;

    virtual void update(const BasePoint& other) = 0;
    virtual std::string toJson() const = 0;
    virtual void fromJson(const std::string& json) = 0;
};

struct YcPoint : public BasePoint {
    std::string code_;
    std::string name_;
    DecodeCode decodeCode_{DecodeCode::UINT16};
    double value_{0.0};
    double rawValue_{0.0};
    double mulCoe_{1.0};
    double addCoe_{0.0};
    double minLimit_{-9999999.0};
    double maxLimit_{9999999.0};
    std::string unit_;
    uint64_t timestamp_{0};
    PointQuality quality_{PointQuality::Good};
    int status_{0};

    std::string code() const override { return code_; }
    std::string name() const override { return name_; }
    PointType type() const override { return PointType::YC; }
    uint64_t timestamp() const override { return timestamp_; }
    PointQuality quality() const override { return quality_; }

    double transformedValue() const {
        return rawValue_ * mulCoe_ + addCoe_;
    }

    void update(const BasePoint& other) override {
        if (other.type() == PointType::YC) {
            const auto& yc = static_cast<const YcPoint&>(other);
            value_ = yc.value_;
            rawValue_ = yc.rawValue_;
            timestamp_ = yc.timestamp_;
            quality_ = yc.quality_;
            status_ = yc.status_;
        }
    }

    std::string toJson() const override;
    void fromJson(const std::string& json) override;
};

struct YxPoint : public BasePoint {
    std::string code_;
    std::string name_;
    DecodeCode decodeCode_{DecodeCode::BOOL};
    int value_{0};
    int bit_{0};
    bool reverse_{false};
    uint64_t timestamp_{0};
    PointQuality quality_{PointQuality::Good};
    int status_{0};

    std::string code() const override { return code_; }
    std::string name() const override { return name_; }
    PointType type() const override { return PointType::YX; }
    uint64_t timestamp() const override { return timestamp_; }
    PointQuality quality() const override { return quality_; }

    void update(const BasePoint& other) override {
        if (other.type() == PointType::YX) {
            const auto& yx = static_cast<const YxPoint&>(other);
            value_ = yx.value_;
            timestamp_ = yx.timestamp_;
            quality_ = yx.quality_;
            status_ = yx.status_;
        }
    }

    std::string toJson() const override;
    void fromJson(const std::string& json) override;
};

struct YkPoint : public BasePoint {
    std::string code_;
    std::string name_;
    DecodeCode decodeCode_{DecodeCode::BOOL};
    int value_{0};
    int commandType_{0};
    YKStatus status_{YKStatus::Idle};
    uint64_t timestamp_{0};
    uint64_t executeTime_{0};
    std::string result_;

    std::string code() const override { return code_; }
    std::string name() const override { return name_; }
    PointType type() const override { return PointType::YK; }
    uint64_t timestamp() const override { return timestamp_; }
    PointQuality quality() const override { return status_ == YKStatus::Success ? PointQuality::Good : PointQuality::Invalid; }

    void update(const BasePoint& other) override {
        if (other.type() == PointType::YK) {
            const auto& yk = static_cast<const YkPoint&>(other);
            value_ = yk.value_;
            commandType_ = yk.commandType_;
            status_ = yk.status_;
            timestamp_ = yk.timestamp_;
            executeTime_ = yk.executeTime_;
            result_ = yk.result_;
        }
    }

    std::string toJson() const override;
    void fromJson(const std::string& json) override;
};

struct YtPoint : public BasePoint {
    std::string code_;
    std::string name_;
    DecodeCode decodeCode_{DecodeCode::UINT16};
    double value_{0.0};
    double mulCoe_{1.0};
    double addCoe_{0.0};
    double minLimit_{-9999999.0};
    double maxLimit_{9999999.0};
    std::string unit_;
    uint64_t timestamp_{0};
    PointQuality quality_{PointQuality::Good};
    int status_{0};

    std::string code() const override { return code_; }
    std::string name() const override { return name_; }
    PointType type() const override { return PointType::YT; }
    uint64_t timestamp() const override { return timestamp_; }
    PointQuality quality() const override { return quality_; }

    void update(const BasePoint& other) override {
        if (other.type() == PointType::YT) {
            const auto& yt = static_cast<const YtPoint&>(other);
            value_ = yt.value_;
            timestamp_ = yt.timestamp_;
            quality_ = yt.quality_;
            status_ = yt.status_;
        }
    }

    std::string toJson() const override;
    void fromJson(const std::string& json) override;
};

struct PointConfig {
    std::string code_;
    std::string name_;
    PointType pointType_{PointType::YC};
    int64_t channelId_{0};
    int64_t deviceId_{0};
    DecodeCode decodeCode_{DecodeCode::UINT16};

    double mulCoe_{1.0};
    double addCoe_{0.0};
    double minLimit_{-9999999.0};
    double maxLimit_{9999999.0};
    std::string unit_;

    int bit_{0};
    bool reverse_{false};
    int commandType_{0};

    std::string description_;
    int status_{1};
    std::string createdAt_;
    std::string updatedAt_;
};

struct ChannelConfig {
    int64_t id_{0};
    std::string name_;
    std::string protocol_;
    std::string connection_;
    std::string settings_;
    int baudRate_{9600};
    int dataBits_{8};
    int stopBits_{1};
    std::string parity_{"none"};
    int status_{1};
};

}
}

#endif
/**
 * @file identity.h
 * @brief 稳定身份基础类型（docs/common/design.md 第 5.2 节）
 *
 * - StationId/AssetId/DeviceId/PointId 等为强类型包装的稳定 ID 文本；
 *   旧数字 ID 转十进制文本，迁移不重新编号。
 * - ConfigVersion 只比较相等；Revision 是 CAS 不透明令牌，禁止当序号排序。
 * - source_epoch/owner_epoch 分别表示样本来源授权代次、控制所有权代次，不可互换。
 * - sequence 为同一已授权来源代次内递增的 uint64，换来源不能直接比较。
 * - event_id/intent_id/command_id 在持久受理前生成，重试保持不变。
 * - ID 内容不用于推导类型、权限或设备层级。
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace ems::core {

/// 强类型稳定 ID 包装（文本）
template <typename Tag>
class StrongId {
   public:
    StrongId() = default;
    explicit StrongId(std::string value) : value_(std::move(value)) {}

    /// 旧数字 ID → 十进制文本
    static StrongId from_number(std::uint64_t numeric) { return StrongId(std::to_string(numeric)); }

    const std::string& value() const noexcept { return value_; }
    bool empty() const noexcept { return value_.empty(); }
    explicit operator bool() const noexcept { return !value_.empty(); }

    bool operator==(const StrongId& other) const noexcept { return value_ == other.value_; }
    bool operator!=(const StrongId& other) const noexcept { return value_ != other.value_; }
    /// 仅用于容器排序；ID 文本不承载业务顺序语义
    bool operator<(const StrongId& other) const noexcept { return value_ < other.value_; }

   private:
    std::string value_;
};

struct StationIdTag {};
struct EssUnitIdTag {};
struct AssetIdTag {};
struct DeviceIdTag {};
struct PointIdTag {};
struct ChannelIdTag {};
struct EventIdTag {};
struct IntentIdTag {};
struct CommandIdTag {};
struct ConfigVersionTag {};

using StationId = StrongId<StationIdTag>;          ///< 站
using EssUnitId = StrongId<EssUnitIdTag>;          ///< 储能单元（调度资源）
using AssetId = StrongId<AssetIdTag>;              ///< 资产节点
using DeviceId = StrongId<DeviceIdTag>;            ///< 设备
using PointId = StrongId<PointIdTag>;              ///< 标准点实例
using ChannelId = StrongId<ChannelIdTag>;          ///< 通道
using EventId = StrongId<EventIdTag>;              ///< 事件（持久受理前生成，重试不变）
using IntentId = StrongId<IntentIdTag>;            ///< 控制意图
using CommandId = StrongId<CommandIdTag>;          ///< 控制命令
using ConfigVersion = StrongId<ConfigVersionTag>;  ///< 不可变配置版本，只比较相等

/// 实时记录 CAS 不透明令牌；禁止当序号排序使用
struct Revision {
    std::string opaque;

    bool empty() const noexcept { return opaque.empty(); }
    bool operator==(const Revision& other) const noexcept { return opaque == other.opaque; }
    bool operator!=(const Revision& other) const noexcept { return opaque != other.opaque; }
};

/// 代次包装：source_epoch（样本来源授权）与 owner_epoch（控制所有权）不可互换
struct Epoch {
    std::uint64_t value{0};

    bool valid() const noexcept { return value != 0; }
    bool operator==(const Epoch& other) const noexcept { return value == other.value; }
    bool operator!=(const Epoch& other) const noexcept { return value != other.value; }
};

using SourceEpoch = Epoch;  ///< 样本来源授权代次
using OwnerEpoch = Epoch;   ///< 控制所有权代次

/// 同一已授权来源代次内递增序号；换来源不能直接比较
using Sequence = std::uint64_t;

}  // namespace ems::core

namespace std {

/// 支持放入 unordered 容器
template <typename Tag>
struct hash<ems::core::StrongId<Tag>> {
    size_t operator()(const ems::core::StrongId<Tag>& id) const noexcept {
        return std::hash<std::string>{}(id.value());
    }
};

}  // namespace std

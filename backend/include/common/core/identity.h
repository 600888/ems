/**
 * @file identity.h
 * @brief 稳定身份基础类型（website/docs/common/design.md 第 5.2 节）
 *
 * - StationId/EssUnitId/DeviceId/PointId 等为强类型包装的稳定 ID 文本；
 *   旧数字 ID 转十进制文本，迁移不重新编号。
 * - 设备树节点（站/单元/设备/部件）统一使用 DeviceId，与 cfg_device.device_id
 *   一致；没有资产概念。
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

/** @brief 以文本保存且不与其他 Tag 类型混用的稳定 ID。 */
template <typename Tag>
class StrongId {
   public:
    /** @brief 构造空 ID。 */
    StrongId() = default;

    /**
     * @brief 从稳定文本构造 ID。
     * @param[in] value 输入值。
     */
    explicit StrongId(std::string value) : value_(std::move(value)) {}

    /**
     * @brief 将旧数字 ID 转为十进制文本，保持原有编号。
     * @param[in] numeric 数值输入。
     * @return 得到的稳定 ID。
     */
    static StrongId from_number(std::uint64_t numeric) { return StrongId(std::to_string(numeric)); }

    /**
     * @brief 返回底层 ID 文本。
     * @return 保存的 ID 或文本值。
     */
    const std::string& value() const noexcept { return value_; }

    /**
     * @brief 判断 ID 文本是否为空。
     * @return true 表示对象为空；false 表示非空。
     */
    bool empty() const noexcept { return value_.empty(); }

    /**
     * @brief 判断 ID 是否非空。
     * @return true 表示对象非空且有效；false 表示为空。
     */
    explicit operator bool() const noexcept { return !value_.empty(); }

    /**
     * @brief 按 ID 文本判等。
     * @param[in] other 用于比较的另一个对象。
     * @return true 表示两者相等；false 表示不相等。
     */
    bool operator==(const StrongId& other) const noexcept { return value_ == other.value_; }

    /**
     * @brief 按 ID 文本判不等。
     * @param[in] other 用于比较的另一个对象。
     * @return true 表示两者不相等；false 表示相等。
     */
    bool operator!=(const StrongId& other) const noexcept { return value_ != other.value_; }

    /**
     * @brief 按文本排序供容器使用，不代表业务顺序。
     * @param[in] other 用于比较的另一个对象。
     * @return true 表示左侧值按定义顺序小于右侧值。
     */
    bool operator<(const StrongId& other) const noexcept { return value_ < other.value_; }

   private:
    std::string value_;
};

struct StationIdTag {};

struct EssUnitIdTag {};

struct DeviceIdTag {};

struct PointIdTag {};

struct ChannelIdTag {};

struct EventIdTag {};

struct IntentIdTag {};

struct CommandIdTag {};

struct ConfigVersionTag {};

using StationId = StrongId<StationIdTag>;          ///< 站
using EssUnitId = StrongId<EssUnitIdTag>;          ///< 储能单元（调度资源）
using DeviceId = StrongId<DeviceIdTag>;            ///< 设备树节点（站/单元/设备/部件共用）
using PointId = StrongId<PointIdTag>;              ///< 标准点实例
using ChannelId = StrongId<ChannelIdTag>;          ///< 通道
using EventId = StrongId<EventIdTag>;              ///< 事件（持久受理前生成，重试不变）
using IntentId = StrongId<IntentIdTag>;            ///< 控制意图
using CommandId = StrongId<CommandIdTag>;          ///< 控制命令
using ConfigVersion = StrongId<ConfigVersionTag>;  ///< 不可变配置版本，只比较相等

/// 实时记录 CAS 不透明令牌；禁止当序号排序使用
struct Revision {
    std::string opaque;

    /**
     * @brief 判断 CAS 令牌是否为空。
     * @return true 表示对象为空；false 表示非空。
     */
    bool empty() const noexcept { return opaque.empty(); }

    /**
     * @brief 比较 CAS 令牌是否相同。
     * @param[in] other 用于比较的另一个对象。
     * @return true 表示两者相等；false 表示不相等。
     */
    bool operator==(const Revision& other) const noexcept { return opaque == other.opaque; }

    /**
     * @brief 比较 CAS 令牌是否不同。
     * @param[in] other 用于比较的另一个对象。
     * @return true 表示两者不相等；false 表示相等。
     */
    bool operator!=(const Revision& other) const noexcept { return opaque != other.opaque; }
};

/// 代次包装：source_epoch（样本来源授权）与 owner_epoch（控制所有权）不可互换
struct Epoch {
    std::uint64_t value{0};

    /**
     * @brief 判断代次是否有效。零为无效代次。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool valid() const noexcept { return value != 0; }

    /**
     * @brief 比较代次值是否相同。
     * @param[in] other 用于比较的另一个对象。
     * @return true 表示两者相等；false 表示不相等。
     */
    bool operator==(const Epoch& other) const noexcept { return value == other.value; }

    /**
     * @brief 比较代次值是否不同。
     * @param[in] other 用于比较的另一个对象。
     * @return true 表示两者不相等；false 表示相等。
     */
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
    /**
     * @brief 按稳定文本计算哈希值。
     * @return 计算得到的数值。
     */
    size_t operator()(const ems::core::StrongId<Tag>& id) const noexcept {
        return std::hash<std::string>{}(id.value());
    }
};

}  // namespace std

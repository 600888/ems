/**
 * @file command.h
 * @brief 控制意图/命令/回执模型（design.md 第 6.1 节）
 *
 * 公共库只定义控制意图、命令、回执的类型和持久化接口；
 * 策略负责仲裁，采集负责执行。模型无执行方法——写实时状态
 * 不能触发设备动作。状态值沿用系统架构定义的生命周期。
 */
#pragma once

#include <common/core/identity.h>
#include <common/core/time.h>
#include <common/model/point.h>

#include <optional>
#include <string>

namespace ems::model {

/// 命令生命周期状态（沿用系统架构；只增不改义）
enum class CommandStatus : std::uint8_t {
    Created = 0,    ///< 已受理（持久化）
    Issued = 1,     ///< 已下发采集执行
    Acked = 2,      ///< 协议确认
    Executing = 3,  ///< 执行中
    Succeeded = 4,  ///< 成功
    Failed = 5,     ///< 失败（终态）
    Rejected = 6,   ///< 被拒绝（终态）
    Expired = 7,    ///< 超期（终态）
    Unknown = 8     ///< 结果未知（提交响应丢失等）
};

/**
 * @brief 返回命令状态的稳定诊断名称。
 * @param[in] s 待解析的文本。
 * @return 对应对象的指针。
 */
inline const char* to_string(CommandStatus s) noexcept {
    switch (s) {
        case CommandStatus::Created:
            return "Created";
        case CommandStatus::Issued:
            return "Issued";
        case CommandStatus::Acked:
            return "Acked";
        case CommandStatus::Executing:
            return "Executing";
        case CommandStatus::Succeeded:
            return "Succeeded";
        case CommandStatus::Failed:
            return "Failed";
        case CommandStatus::Rejected:
            return "Rejected";
        case CommandStatus::Expired:
            return "Expired";
        case CommandStatus::Unknown:
            return "Unknown";
    }
    return "Unknown";
}

/// 控制意图（尚未允许执行；策略仲裁后才产生命令）
struct ControlIntent {
    core::IntentId intent_id;       ///< 持久受理前生成，重试保持不变
    std::string capability_code;    ///< 目标能力
    core::AssetId target_asset_id;  ///< 目标资产
    std::optional<core::DeviceId> target_device_id;
    ValueType requested_type{ValueType::Double};  ///< 请求值类型
    PointValue requested_value;                   ///< 请求值
    std::string actor;                            ///< 来源/操作者（审计上下文）
    std::string reason;                           ///< 原因
    core::UtcMicros created_time{core::kInvalidUtcMicros};
    core::UtcMicros deadline_time{core::kInvalidUtcMicros};  ///< 期限
    std::string config_version;                              ///< 期望的配置版本
};

/// 控制命令（仅策略产生；目标能力 + 所有权 + 期限）
struct Command {
    core::CommandId command_id;  ///< 持久受理前生成，重试保持不变
    core::IntentId intent_id;    ///< 溯源意图
    core::AssetId target_asset_id;
    std::optional<core::DeviceId> target_device_id;
    std::string capability_code;
    std::string expected_config_version;                  ///< 命令执行期望的配置版本
    core::OwnerEpoch owner_epoch;                         ///< 控制所有权代次
    std::uint32_t priority{0};                            ///< 优先级（越大越高）
    core::UtcMicros expires_at{core::kInvalidUtcMicros};  ///< 过期时间
};

/// 命令回执（按状态机追加事件）
struct CommandResult {
    core::CommandId command_id;
    CommandStatus status{CommandStatus::Created};
    std::string reason;            ///< 失败/拒绝原因
    std::string protocol_receipt;  ///< 协议确认/设备反馈（诊断载荷）
    core::UtcMicros result_time{core::kInvalidUtcMicros};
};

}  // namespace ems::model

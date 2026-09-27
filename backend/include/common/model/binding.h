/**
 * @file binding.h
 * @brief 绑定模型：设备/通道多对多、厂家点路由、北向转发（design.md 第 6.1 节）
 *
 * - 设备/通道多对多；共享总线地址属于绑定，不属于设备全局属性。
 * - 标准点身份独立于通信路径：厂家原始值只作为明确诊断载荷。
 * - 南向厂家点路由（VendorPointRoute）与北向转发映射（ForwardingPointMap）分开。
 */
#pragma once

#include <common/core/identity.h>
#include <common/model/channel.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ems::model {

/// 绑定支持的操作集合
enum class BindingOp : std::uint8_t {
    Collect = 0,  ///< 采集（南向遥测/状态）
    Control = 1,  ///< 控制（南向命令）
    Forward = 2   ///< 转发（北向）
};

/**
 * @brief 返回设备通道绑定操作的稳定诊断名称。
 * @param[in] op 数据库操作。
 * @return 对应对象的指针。
 */
inline const char* to_string(BindingOp op) noexcept {
    switch (op) {
        case BindingOp::Collect:
            return "Collect";
        case BindingOp::Control:
            return "Control";
        case BindingOp::Forward:
            return "Forward";
    }
    return "Unknown";
}

/// 设备-通道绑定（多对多；协议地址是绑定的属性）
struct DeviceChannelBinding {
    std::string binding_id;  ///< 稳定绑定 ID
    core::DeviceId device_id;
    core::ChannelId channel_id;
    std::vector<BindingOp> operations;  ///< 操作集合
    std::string protocol_address;       ///< 设备在该通道上的协议地址（共享总线地址属于绑定）
    std::string version;                ///< 绑定版本
    bool enabled{true};
};

/// 厂家点路由：厂家点身份 → 标准点（南向）
struct VendorPointRoute {
    std::string route_id;             ///< 稳定路由 ID
    core::PointId point_id;           ///< 标准点身份（独立于通信路径）
    std::string binding_id;           ///< 引用设备-通道绑定
    std::string vendor_address;       ///< 厂家点身份/寄存器地址等
    std::string template_key;         ///< 设备内稳定厂家点键，重复导入据此更新
    std::string vendor_name;          ///< 厂家点显示名称
    std::string protocol;             ///< 首期导入为 modbus
    std::string address_space;        ///< coil/discrete_input/input_register/holding_register
    std::uint32_t address_offset{0};  ///< 零基协议偏移
    std::string wire_type;            ///< bool/i16/u16/i32/u32/f32
    std::string byte_order;           ///< 多寄存器字节序；单寄存器为空
    double scale{1.0};                ///< 逻辑转换：标准值 = 原始值 * scale + offset
    double offset{0.0};
    bool writable{false};  ///< 是否可作为控制下发路径
    bool enabled{true};
};

/// 转发方向
enum class ForwardDirection : std::uint8_t {
    Upstream = 0,    ///< 上送北向
    Downstream = 1,  ///< 北向下写
    Bidirectional = 2
};

/// 北向转发映射：标准点 → 北向协议地址（与南向厂家点路由分开）
struct ForwardingPointMap {
    std::string map_id;         ///< 稳定映射 ID
    core::PointId point_id;     ///< 标准点
    std::string north_address;  ///< 北向协议地址
    ForwardDirection direction{ForwardDirection::Upstream};
    bool enabled{true};
};

}  // namespace ems::model

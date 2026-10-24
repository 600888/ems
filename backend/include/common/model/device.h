/**
 * @file device.h
 * @brief 设备树节点模型（与 cfg_device 单树一致；website/docs/common/database/device-management.md）
 *
 * 只有设备，没有资产：站、单元、设备、部件统一在 Device 一个结构里，
 * 用 node_kind 区分，parent_device_id 自引用成树（站根为空）。部件
 * （电池簇/包/电芯）同样有稳定 ID，可挂点但无独立协议连接，其采集通信
 * 设备在配置侧通过 source_device_id 表达。
 */
#pragma once

#include <common/model/point.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ems::model {

/// 设备树节点种类（与 cfg_device.node_kind 一致）
enum class DeviceNodeKind : std::uint8_t {
    Station = 0,   ///< 站根
    Unit = 1,      ///< 储能单元
    Device = 2,    ///< 通信设备
    Component = 3  ///< 部件（电池簇/包/电芯，可有点但无独立协议连接）
};

/**
 * @brief 返回设备树节点种类的稳定诊断名称。
 * @param[in] k 节点种类。
 * @return 对应对象的指针；未知值返回 "Unknown"。
 */
inline const char* to_string(DeviceNodeKind k) noexcept {
    switch (k) {
        case DeviceNodeKind::Station:
            return "Station";
        case DeviceNodeKind::Unit:
            return "Unit";
        case DeviceNodeKind::Device:
            return "Device";
        case DeviceNodeKind::Component:
            return "Component";
    }
    return "Unknown";
}

/// 设备树节点（站/单元/设备/部件共用；运行状态见 realtime 专题）
struct Device {
    core::DeviceId device_id;         ///< 稳定节点 ID，全树唯一
    core::DeviceId parent_device_id;  ///< 父节点；站根为空
    DeviceNodeKind node_kind{DeviceNodeKind::Device};
    std::string code;  ///< 同父节点下唯一的业务编码
    std::string name;
    std::string type_version_id;   ///< DeviceTypeDefinition 版本引用；设备/部件必填
    std::string model_version_id;  ///< 厂家型号版本
    std::string vendor;            ///< 厂家
    std::string firmware_version;  ///< 固件信息
    bool enabled{true};
};

/// 设备类型定义（版本化；PCS/BMS/电表等目录引用设备模型专题）
struct DeviceTypeDefinition {
    std::string type_version_id;  ///< 版本化类型标识
    std::string type_code;        ///< 类型编码，如 pcs / bms / meter
    std::string name;
    std::vector<StandardPointDefinition> standard_points;  ///< 标准点目录
    std::vector<std::string> capability_codes;             ///< 声明支持的能力（依赖）
    bool enabled{true};
};

}  // namespace ems::model

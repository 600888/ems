/**
 * @file device.h
 * @brief 设备模型（design.md 第 6.1/6.2 节）
 *
 * 设备是公共模型中的身份和版本化定义，不持有 socket、线程、数据库
 * 连接、协议句柄。设备通过 DeviceChannelBinding 引用通道，不维护
 * 另一棵通道对象树。code 从资产节点解析。
 */
#pragma once

#include <string>
#include <vector>

#include <common/model/point.h>

namespace ems::model {

/// 设备（身份 + 版本化定义；运行状态见 realtime 专题）
struct Device {
    core::DeviceId device_id;
    core::AssetId asset_id;        ///< 资产归属
    std::string code;              ///< 从资产节点解析
    std::string name;
    std::string type_version_id;   ///< DeviceTypeDefinition 版本引用
    std::string model_version_id;  ///< 厂家型号版本
    std::string vendor;            ///< 厂家
    std::string firmware_version;  ///< 固件信息
    bool enabled{true};
};

/// 设备类型定义（版本化；PCS/BMS/电表等目录引用设备模型专题）
struct DeviceTypeDefinition {
    std::string type_version_id;    ///< 版本化类型标识
    std::string type_code;          ///< 类型编码，如 pcs / bms / meter
    std::string name;
    std::vector<StandardPointDefinition> standard_points;  ///< 标准点目录
    std::vector<std::string> capability_codes;             ///< 声明支持的能力（依赖）
    bool enabled{true};
};

}  // namespace ems::model

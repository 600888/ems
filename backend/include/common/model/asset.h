/**
 * @file asset.h
 * @brief 资产模型（design.md 第 6.1 节）
 *
 * 资产关系使用独立模型：唯一主要父节点、无环、不能跨站挂接。
 * 资产树与电气拓扑分开表达，不把所有关系压进 parent_asset_id。
 * 电池部件可以有测点而没有独立通信设备（挂在资产而非设备上）。
 */
#pragma once

#include <common/core/identity.h>

#include <optional>
#include <string>

namespace ems::model {

/// 资产类型
enum class AssetType : std::uint8_t {
    Station = 0,  ///< 站
    EssUnit = 1,  ///< 储能单元（调度资源，不固定等同一柜一 PCS）
    Device = 2,   ///< 设备
    Part = 3,     ///< 部件（如电池簇，可有测点无独立通信设备）
    Other = 99
};

/// 资产节点（树）
struct AssetNode {
    core::AssetId asset_id;
    core::StationId station_id;                    ///< 所属站（不能跨站挂接）
    std::optional<core::AssetId> parent_asset_id;  ///< 唯一主要父节点；根为空
    std::string code;                              ///< 可读业务编码（引用 Redis 键专题规则）
    std::string name;                              ///< 显示名称
    AssetType asset_type{AssetType::Other};
};

/// 储能单元扩展属性（asset_type == EssUnit 时有效）
struct EssUnitInfo {
    core::AssetId asset_id;          ///< 对应资产节点
    std::string timezone;            ///< 站时区（IANA 名称）
    std::string grid_access_point;   ///< 接入点
    double rated_power_kw{0.0};      ///< 额定功率
    double rated_capacity_kwh{0.0};  ///< 额定容量
};

}  // namespace ems::model

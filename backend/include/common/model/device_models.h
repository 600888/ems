/**
 * @file device_models.h
 * @brief 设备业务模型继承层次；固有点与能力只由具体设备子类定义。
 */
#pragma once

#include <common/model/device_config.h>

#include <string>
#include <vector>

namespace ems::model {

/// 设备业务模型基类，只保存所有类型共有的标识与版本信息。
class DeviceModel {
   public:
    /** @brief 支持通过基类指针安全销毁具体设备模型。 */
    virtual ~DeviceModel() = default;

    /**
     * @brief 返回稳定的设备类型 code。
     * @return 设备类型 code。
     */
    const std::string& type_code() const noexcept { return type_code_; }

    /**
     * @brief 返回不可变类型版本标识。
     * @return 类型版本标识。
     */
    const std::string& type_version_id() const noexcept { return type_version_id_; }

    /**
     * @brief 返回设备类型的显示名称。
     * @return 类型名称。
     */
    const std::string& name() const noexcept { return name_; }

   protected:
    /**
     * @brief 初始化所有设备共有的类型元数据。
     * @param[in] type_code 稳定设备类型 code。
     * @param[in] type_version_id 不可变类型版本标识。
     * @param[in] name 类型显示名称。
     */
    DeviceModel(std::string type_code, std::string type_version_id, std::string name);

   private:
    std::string type_code_;
    std::string type_version_id_;
    std::string name_;
};

/// PCS 业务模型：定义 PCS 自己的固有点和能力。
class PcsDeviceModel final : public DeviceModel {
   public:
    /** @brief 构造首期 PCS 类型模型。 */
    PcsDeviceModel();

    /**
     * @brief 生成 PCS 固有点及能力 code 的版本化目录。
     * @return PCS 类型定义值对象。
     */
    DeviceTypeDefinition definition() const;

    /**
     * @brief 生成 PCS 固有能力的点位依赖。
     * @return PCS 能力依赖目录。
     */
    std::vector<StandardCapability> capabilities() const;
};

/// BMS 业务模型：定义 BMS 自己的固有点和能力。
class BmsDeviceModel final : public DeviceModel {
   public:
    /** @brief 构造首期 BMS 类型模型。 */
    BmsDeviceModel();

    /**
     * @brief 生成 BMS 固有点及能力 code 的版本化目录。
     * @return BMS 类型定义值对象。
     */
    DeviceTypeDefinition definition() const;

    /**
     * @brief 生成 BMS 固有能力的点位依赖。
     * @return BMS 能力依赖目录。
     */
    std::vector<StandardCapability> capabilities() const;
};

/// 消防设备业务模型：定义消防设备自己的固有点和监视能力。
class FireDeviceModel final : public DeviceModel {
   public:
    /** @brief 构造首期消防设备类型模型。 */
    FireDeviceModel();

    /**
     * @brief 生成消防设备固有点及能力 code 的版本化目录。
     * @return 消防设备类型定义值对象。
     */
    DeviceTypeDefinition definition() const;

    /**
     * @brief 生成消防监视能力的点位依赖。
     * @return 消防设备能力依赖目录。
     */
    std::vector<StandardCapability> capabilities() const;
};

}  // namespace ems::model

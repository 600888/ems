/** @file protocol_handler.h
 * @brief 采集与上送使用的协议无关处理器接口；不属于动态库 ABI。
 */
#ifndef EMS_NET_HANDLER_PROTOCOL_HANDLER_H
#define EMS_NET_HANDLER_PROTOCOL_HANDLER_H

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "net/protocol/protocol_types.h"

namespace ems::net::collect {
class PluginManager;
}

namespace ems::net::handler {
using protocol::SampleQuality;
using json = nlohmann::json;

/** @brief 按输入顺序返回的采集样本；无效值以 quality 标识。 */
struct PointValue {
    std::string code;
    double value{0};
    std::int64_t rawValue{0};
    int quality{enum_value(SampleQuality::Invalid)};
    std::uint64_t timestamp{0};
    json details;  ///< 原始插件结果，保留需量时间、复合字段与协议品质。
};

/** @brief 处理器统一接口，管理器须比处理器及所有在途调用存活更久。 */
class ProtocolHandler {
   public:
    /** @brief 停止通信并释放所拥有的插件实例。 */
    virtual ~ProtocolHandler() = default;

    /**
     * @brief 禁止复制实例所有者。
     * @param[in] other 源对象。
     */
    ProtocolHandler(const ProtocolHandler& other) = delete;

    /**
     * @brief 禁止复制赋值。
     * @param[in] other 源对象。
     * @return 不可调用。
     */
    ProtocolHandler& operator=(const ProtocolHandler& other) = delete;

    /**
     * @brief 校验配置并按需加载对应协议插件。
     * @param[in] config 端点、角色、测点和协议参数；settings 支持对象或 JSON 字符串。
     * @return true 成功；false 失败，可查询 last_error；失败保留之前的有效实例。
     */
    virtual bool initialize(const json& config) = 0;

    /**
     * @brief 建立客户端连接或启动服务端监听。
     * @return true 成功；false 失败，可查询 last_error。
     */
    virtual bool connect() = 0;

    /**
     * @brief 停止通信，保留配置以便重新连接。
     * @return true 已断开；false 停止失败，继续保留实例和库租约。
     */
    virtual bool disconnect() = 0;

    /**
     * @brief 查询真实通信状态。
     * @return true 已连接或正在监听。
     */
    virtual bool is_connected() const = 0;

    /**
     * @brief 批量读取，负责 ABI 缓冲扩容和结果转换。
     * @param[in] point_codes 协议测点编码列表。
     * @param[out] results 与输入顺序一致的样本；调用失败时清空。
     * @return 有效数值点数；-1 表示连接、协议或结果格式错误。
     */
    virtual int read_batch(const std::vector<std::string>& point_codes,
                           std::vector<PointValue>& results) = 0;

    /**
     * @brief 客户端下发控制或服务端更新本地映像。
     * @param[in] point_code 协议地址或配置的测点别名。
     * @param[in] value 协议物理值；具体范围及写权限由对应插件校验。
     * @return true 成功；false 拒绝或失败，可查询 last_error。
     */
    virtual bool write_value(const std::string& point_code, double value) = 0;

    /**
     * @brief 返回本次操作的诊断副本。
     * @return 错误说明；最近操作成功时为空。
     */
    virtual std::string last_error() const = 0;

    /**
     * @brief 返回处理器使用的协议映射名称。
     * @return ModbusTCP、ModbusRTU、IEC104、DLT645 或已注册的自定义名称。
     */
    virtual std::string protocol_name() const = 0;

   protected:
    /** @brief 创建尚未初始化的派生处理器。 */
    ProtocolHandler() = default;
};

/**
 * @brief 创建对应的处理器；已知协议使用专用类，扩展协议使用公共 ABI 适配器。
 * @param[in,out] manager 共享插件管理器，须比返回对象存活更久。
 * @param[in] protocol_name 协议映射名称；不能为空。
 * @return 调用方独占的未初始化处理器；无动态库加载副作用。
 * @throws std::invalid_argument 协议名称为空。
 */
std::unique_ptr<ProtocolHandler> create_handler(collect::PluginManager& manager,
                                                const std::string& protocol_name);
}  // namespace ems::net::handler
#endif

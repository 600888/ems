/**
 * @file protocol_handler.h
 * @brief 协议插件 C++ 抽象基类
 *
 * 每个 .so 内部使用此抽象类实现协议逻辑，
 * 通过 protocol_plugin_api.h 中的 C 导出函数对外暴露。
 * 采集引擎不直接引用此头文件，仅通过 C 接口与插件交互。
 */
#ifndef EMS_NET_PROTOCOL_HANDLER_H
#define EMS_NET_PROTOCOL_HANDLER_H

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace ems {
namespace net {
namespace protocol {

using json = nlohmann::json;

/**
 * @brief 测点读取结果
 */
struct PointValue {
    std::string code;       // 测点编码
    double value{0.0};      // 工程值
    int64_t rawValue{0};    // 原始值
    int quality{0};         // 品质 0=Good, 1=Invalid
    uint64_t timestamp{0};  // 采集时间戳 (ms)
};

/**
 * @brief 测点写入任务
 */
struct WriteTask {
    std::string pointCode;
    double value{0.0};
    int timeoutMs{5000};
};

/**
 * @brief 协议处理器抽象基类
 *
 * 每种协议实现一个子类，注册工厂函数通过 C 接口暴露。
 * 生命周期: create -> init -> connect -> [read/write]* -> disconnect -> destroy
 */
class ProtocolHandler {
   public:
    /** @brief 销毁协议插件处理器。 */
    virtual ~ProtocolHandler() = default;

    /**
     * @brief 禁止复制协议处理器。
     * @param[in] other 待复制或移动的源对象。
     */
    ProtocolHandler(const ProtocolHandler& other) = delete;

    /**
     * @brief 禁止复制赋值协议处理器。
     * @param[in] other 待复制或移动的源对象。
     */
    ProtocolHandler& operator=(const ProtocolHandler& other) = delete;

    /**
     * @brief 转移协议处理器状态。
     * @param[in] other 待复制或移动的源对象。
     */
    ProtocolHandler(ProtocolHandler&& other) noexcept = default;

    /**
     * @brief 通过移动赋值转移协议处理器状态。
     * @param[in] other 待复制或移动的源对象。
     */
    ProtocolHandler& operator=(ProtocolHandler&& other) noexcept = default;

    /**
     * @brief 初始化插件（解析 JSON 配置）
     * @param[in] config JSON 配置对象
     * @return true 成功
     */
    virtual bool initialize(const json& config) = 0;

    /**
     * @brief 连接设备
     * @return true 连接成功
     */
    virtual bool connect() = 0;

    /**
     * @brief 断开连接
     * @return true 表示操作成功；false 表示失败。
     */
    virtual bool disconnect() = 0;

    /**
     * @brief 是否已连接
     * @return true 表示条件成立；false 表示不成立。
     */
    virtual bool is_connected() const = 0;

    /**
     * @brief 批量读取测点值
     *
     * 插件应尽量合并连续地址的读请求以减少网络 I/O，
     * 保证输出的 results 与输入的 point_codes 一一对应。
     *
     * @param[in] point_codes 测点编码列表
     * @param[out] results     [out] 读取结果（与 point_codes 等长）
     * @return 成功读取的测点数, -1 表示失败
     */
    virtual int read_batch(const std::vector<std::string>& point_codes,
                           std::vector<PointValue>& results) = 0;

    /**
     * @brief 写入单个测点值
     * @param[in] point_code 测点编码
     * @param[in] value      工程值
     * @return true 写入成功
     */
    virtual bool write_value(const std::string& point_code, double value) = 0;

    /**
     * @brief 获取最近一次错误信息
     * @return 最近一次错误的说明文本。
     */
    virtual std::string last_error() const = 0;

    /**
     * @brief 获取协议名称
     * @return 协议名称。
     */
    virtual std::string protocol_name() const = 0;

   protected:
    /** @brief 构造供派生插件使用的处理器基类。 */
    ProtocolHandler() = default;
};

/**
 * @brief 协议插件工厂
 *
 * 每个 .so 应定义一个全局工厂实例，
 * 在 C 导出函数 ems_plugin_create 中调用。
 * @return 创建的对象；创建失败时为空。
 */
template <typename T, typename = std::enable_if_t<std::is_base_of_v<ProtocolHandler, T>>>
ProtocolHandler* create_handler() {
    return new T();
}

/**
 * @brief 销毁处理器
 * @param[in] handler 待销毁的协议处理器。
 */
inline void destroy_handler(ProtocolHandler* handler) { delete handler; }

}  // namespace protocol
}  // namespace net
}  // namespace ems

#endif  // EMS_NET_PROTOCOL_HANDLER_H

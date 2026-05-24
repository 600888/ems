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

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <optional>
#include <nlohmann/json.hpp>

namespace ems {
namespace net {
namespace protocol {

using json = nlohmann::json;

/**
 * @brief 测点读取结果
 */
struct PointValue {
    std::string code;            // 测点编码
    double      value{0.0};      // 工程值
    int64_t     rawValue{0};     // 原始值
    int         quality{0};      // 品质 0=Good, 1=Invalid
    uint64_t    timestamp{0};    // 采集时间戳 (ms)
};

/**
 * @brief 测点写入任务
 */
struct WriteTask {
    std::string pointCode;
    double      value{0.0};
    int         timeoutMs{5000};
};

/**
 * @brief 协议处理器抽象基类
 *
 * 每种协议实现一个子类，注册工厂函数通过 C 接口暴露。
 * 生命周期: create -> init -> connect -> [read/write]* -> disconnect -> destroy
 */
class ProtocolHandler {
public:
    virtual ~ProtocolHandler() = default;

    // ---- 拷贝/赋值 禁用 ----
    ProtocolHandler(const ProtocolHandler&) = delete;
    ProtocolHandler& operator=(const ProtocolHandler&) = delete;
    ProtocolHandler(ProtocolHandler&&) noexcept = default;
    ProtocolHandler& operator=(ProtocolHandler&&) noexcept = default;

    /**
     * @brief 初始化插件（解析 JSON 配置）
     * @param config JSON 配置对象
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
     */
    virtual bool disconnect() = 0;

    /**
     * @brief 是否已连接
     */
    virtual bool is_connected() const = 0;

    /**
     * @brief 批量读取测点值
     *
     * 插件应尽量合并连续地址的读请求以减少网络 I/O，
     * 保证输出的 results 与输入的 point_codes 一一对应。
     *
     * @param point_codes 测点编码列表
     * @param results     [out] 读取结果（与 point_codes 等长）
     * @return 成功读取的测点数, -1 表示失败
     */
    virtual int read_batch(
        const std::vector<std::string>& point_codes,
        std::vector<PointValue>&         results
    ) = 0;

    /**
     * @brief 写入单个测点值
     * @param point_code 测点编码
     * @param value      工程值
     * @return true 写入成功
     */
    virtual bool write_value(const std::string& point_code, double value) = 0;

    /**
     * @brief 获取最近一次错误信息
     */
    virtual std::string last_error() const = 0;

    /**
     * @brief 获取协议名称
     */
    virtual std::string protocol_name() const = 0;

protected:
    ProtocolHandler() = default;
};

/**
 * @brief 协议插件工厂
 *
 * 每个 .so 应定义一个全局工厂实例，
 * 在 C 导出函数 ems_plugin_create 中调用。
 */
template<typename T, typename = std::enable_if_t<std::is_base_of_v<ProtocolHandler, T>>>
ProtocolHandler* create_handler() {
    return new T();
}

/**
 * @brief 销毁处理器
 */
inline void destroy_handler(ProtocolHandler* handler) {
    delete handler;
}

} // namespace protocol
} // namespace net
} // namespace ems

#endif // EMS_NET_PROTOCOL_HANDLER_H

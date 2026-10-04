/** @file dlt645_protocol.h
 * @brief DL/T 645-2007 PIMPL API；公开头不依赖 Boost、协议栈或 JSON。
 */
#ifndef EMS_NET_PROTOCOL_DLT645_PROTOCOL_H
#define EMS_NET_PROTOCOL_DLT645_PROTOCOL_H

#include <memory>
#include <string>
#include <vector>

namespace ems::net::protocol {

/** @brief 每实例独占 TCP/RTU 客户端或只读服务端；调用方须串行调用 API。 */
class Dlt645Protocol final {
   public:
    /** @brief 创建未配置实例，不打开网络或串口。 */
    Dlt645Protocol();

    /** @brief 停止通信并等待线程退出，再释放内部资源。 */
    ~Dlt645Protocol();

    /**
     * @brief 禁止复制资源所有者。
     * @param[in] other 源实例。
     */
    Dlt645Protocol(const Dlt645Protocol& other) = delete;

    /**
     * @brief 禁止复制赋值。
     * @param[in] other 源实例。
     * @return 不可调用。
     */
    Dlt645Protocol& operator=(const Dlt645Protocol& other) = delete;

    /**
     * @brief 禁止移动活动回调对象。
     * @param[in] other 源实例。
     */
    Dlt645Protocol(Dlt645Protocol&& other) = delete;

    /**
     * @brief 禁止移动赋值。
     * @param[in] other 源实例。
     * @return 不可调用。
     */
    Dlt645Protocol& operator=(Dlt645Protocol&& other) = delete;

    /**
     * @brief 校验并保存配置，兼容通道 settings 对象或 JSON 字符串。
     * @param[in] json_config 角色、TCP/RTU 端点、BCD 表地址、超时和测点配置。
     * @return 0 成功；-1 配置非法或实例已连接，失败不替换旧配置。
     */
    int initialize(const std::string& json_config);

    /**
     * @brief 建立通信或启动只读监听。
     * @return 0 成功；-1 失败并保存诊断。
     */
    int connect();

    /**
     * @brief 停止并等待所有通信线程退出。
     * @return 0 成功；-1 停止失败。
     */
    int disconnect();

    /**
     * @brief 查询通信资源状态。
     * @return true 表示已连接或正在监听。
     */
    bool is_connected() const;

    /**
     * @brief 按输入顺序读取 00–04 类 DI，复合值额外输出字段与发生时间。
     * @param[in] point_codes DI、表地址:DI[:字段索引] 或配置的测点别名。
     * @param[out] result_json JSON 结果；坏点保留 value=null、quality=1。
     * @return 有效数值点数；-1 参数或连接错误；系数换算由点映射层处理。
     */
    int read_batch(const std::vector<std::string>& point_codes, std::string& result_json);

    /**
     * @brief 客户端写已启用的 04 类数值参变量；服务端更新本地只读出口映像。
     * @param[in] point_code 数据地址或配置别名；复合字段须通过配置初始化。
     * @param[in] value 协议物理值，须有限且满足数据格式的范围和精度。
     * @return 0 成功；-1 禁止写、编码非法、越界或通信失败。
     */
    int write(const std::string& point_code, double value);

    /**
     * @brief 返回诊断副本。
     * @return 最近一次错误信息。
     */
    std::string last_error() const;

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ems::net::protocol
#endif

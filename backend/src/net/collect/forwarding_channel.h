#pragma once

#include <memory>

#include "net/para_loader/cfg_loader.h"
#include "net/protocol/plugin_manager.h"
#include "rtdb/client/rtdb.h"

namespace ems::net::collect {
/** @brief 独立工作线程将 RTDB 工程值同步至只读 MMS 映像，不回写实时库。 */
class ForwardingChannel final {
   public:
    /** @brief 保存不可变运行目录，不访问网络。
     * @param[in] config 已校验的通道与映射。
     * @param[in] realtime 借用同一 RTDB 客户端的共享所有权，停止后释放。
     * @param[in,out] manager 插件管理器，须比本对象存活更久。
     */
    ForwardingChannel(device::ForwardingChannelConfig config, std::shared_ptr<rtdb::RtDb> realtime,
                      PluginManager& manager);
    /** @brief 唤醒并等待工作线程，停止监听后释放插件租约。 */
    ~ForwardingChannel();
    /** @brief 初始化无效映像、首次同步后监听并启动线程。
     * @return true 成功；false 失败，部分资源已清理。
     */
    bool start();
    /** @brief 停止工作线程和监听；幂等，完成前不释放 RTDB。 */
    void stop();
    /** @brief 返回线程与监听的实际状态。 @return 是否正常运行。 */
    bool is_running() const;
    /** @brief 获取最近错误副本。 @return 脱敏错误文本，成功同步后清空。 */
    std::string last_error() const;

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace ems::net::collect

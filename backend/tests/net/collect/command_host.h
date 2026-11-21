#pragma once
#include <memory>

#include "net/collect/collect_engine.h"

/** @brief 集成测试使用正式实时库提交和持久执行链路，不暴露通道队列。 */
class CommandHost {
   public:
    /**
     * @brief 打开隔离 SQLite 日志和实时库，启动正式命令服务。
     * @param[in,out] engine 已运行的采集引擎，须比测试宿主存活更久。
     * @param[in] realtime_config 测试部署配置路径。
     * @param[in] scope 站作用域。
     */
    CommandHost(ems::net::collect::CollectEngine& engine, const std::string& realtime_config,
                const std::string& scope);
    /** @brief 停止受理并清理测试日志，设备线程由测试调用方停止。 */
    ~CommandHost();
    /**
     * @brief 通过实时库提交并轮询正式命令服务。
     * @param[in] point_id 稳定命令点 ID。
     * @param[in] value 工程目标值。
     * @param[in] lifetime_ms 提交及执行的总期限。
     * @return 协议执行状态；超时抛出测试异常。
     */
    ems::net::collect::PointCommandResult execute(const std::string& point_id, double value,
                                                  int lifetime_ms = 3000);

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#pragma once
#include <rtdb/client/command_client.h>
#include <rtdb/client/realtime_service.h>

namespace ems::rtdb {
/** @brief 使用统一类型化实时记录的进程客户端；不保留旧 RTDB 键或模型。 */
class EMS_RTDB_API PointClient final {
   public:
    /**
     * @brief 挂接部署后端并注入当前运行目录；只读打开不改变任何来源授权。
     * @param[in] path 部署配置路径。
     * @param[in] catalog 当前站根修订号编译的完整目录。
     * @param[in] ctx 打开预算。
     * @return 共享客户端或明确错误，不创建后端资源。
     */
    static Result<std::shared_ptr<PointClient>> open(const std::string& path,
                                                     std::shared_ptr<const RuntimeCatalog> catalog,
                                                     const CallContext& ctx);
    /** @brief 释放本进程映射；须先等待全部调用结束。 */
    ~PointClient();
    /**
     * @brief 唯一采集执行者在启动或热加载时安装当前来源授权。
     * @param[in] ctx 安装预算。
     * @return 全部授权安装成功或错误；失败时不得启动新控制。
     */
    core::Status installWriter(const CallContext& ctx);
    /**
     * @brief 将采集或反馈读取的工程值写入统一测点记录。
     * @param[in] id 稳定点 ID。
     * @param[in] value 工程值，按配置值类型严格转换。
     * @param[in] quality 公共品质码。
     * @param[in] timestamp_ms 源 UTC 毫秒时间。
     * @param[in] ctx 写入预算。
     * @return 业务写入结果；未安装写来源时返回错误。
     */
    Result<SampleResult> update(const std::string& id, double value, model::Quality quality,
                                std::int64_t timestamp_ms, const CallContext& ctx);
    /**
     * @brief 从同一类型化记录读取四遥样本及命令状态，供 Web、上送与归档共用。
     * @param[in] type 四遥文本 yc/yx/yk/yt。
     * @param[in] id 稳定点 ID。
     * @param[in] ctx 读取预算。
     * @return JSON 样本；不存在、禁用或类型不符返回明确错误。
     */
    Result<std::string> readPointJson(const std::string& type, const std::string& id,
                                      const CallContext& ctx);
    /**
     * @brief 同步命令最新目标与状态；保持实测反馈点独立。
     * @param[in] command 持久事实对应的命令状态。
     * @param[in] order 持久记录主键。
     * @param[in] ctx 投影预算。
     * @return 成功、重复或忽略旧命令；错误须重试补同步。
     */
    Result<SampleResult> projectCommand(const CommandRecord& command, std::int64_t order,
                                        const CallContext& ctx);
    /**
     * @brief 将通道连接状态写入共享的 ChannelState 记录。
     * @param[in] id 稳定通道 ID。
     * @param[in] state 连接状态。
     * @param[in] reason 状态原因。
     * @param[in] ctx 写入预算。
     * @return 写入结果或错误。
     */
    core::Status reportChannel(const std::string& id, ChannelRunState state,
                               const std::string& reason, const CallContext& ctx);
    /** @brief 返回当前目录。 @return 共享只读目录。 */
    std::shared_ptr<const RuntimeCatalog> catalog() const;

   private:
    struct Impl;
    /** @brief 接管已打开实现。 @param[in] impl 实现所有权转入本对象。 */
    explicit PointClient(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace ems::rtdb

#pragma once

#include <common/database/history/types.h>
#include <common/database/i_transaction.h>

namespace ems::database {

/** @brief 精确版本历史目录；所有调用参与调用方事务。 */
class IHistoryCatalogRepository {
   public:
    /** @brief 释放接口，不持有调用方事务。 */
    virtual ~IHistoryCatalogRepository() = default;

    /**
     * @brief 读取 ready 版本中的点位及其逻辑序列映射。
     * @param[in,out] tx 本数据库上下文的活动事务。
     * @param[in] scope_kind station 或 unit。
     * @param[in] scope_id 稳定作用域 ID。
     * @param[in] version 精确配置版本，不回退到当前版本。
     * @param[in] point_id 稳定点位 ID。
     * @param[in] ctx 调用期限。
     * @return 快照；版本未就绪为 NotReady，ready 版本缺点为 InvalidArgument。
     */
    virtual core::Result<PointSnapshot> find_point(ITransaction& tx, const std::string& scope_kind,
                                                   const std::string& scope_id,
                                                   const std::string& version,
                                                   const std::string& point_id,
                                                   const core::CallContext& ctx) = 0;

    /**
     * @brief 读取稳定点的逻辑序列映射，不暴露驱动对象。
     * @param[in,out] tx 本数据库上下文的活动事务。
     * @param[in] point_id 稳定点位 ID。
     * @param[in] ctx 调用期限。
     * @return 映射；未知点为 NotFound。
     */
    virtual core::Result<SeriesMapping> find_series(ITransaction& tx, const std::string& point_id,
                                                    const core::CallContext& ctx) = 0;
};

/** @brief 已到达事件的身份与隔离仓储；不自行提交事务。 */
class IHistoryEventRepository {
   public:
    /** @brief 释放接口，不提交业务事务。 */
    virtual ~IHistoryEventRepository() = default;

    /**
     * @brief 登记完整事件身份；同 ID 同内容幂等，异内容返回 Conflict。
     * @param[in,out] tx 本数据库上下文的活动事务。
     * @param[in] sample 已核验目录的样本，不将样本载荷复制为历史备份。
     * @param[in] ctx 调用期限。
     * @return 暂存结果；调用方成功提交后才可写时序库。
     */
    virtual core::Status register_event(ITransaction& tx, const Sample& sample,
                                        const core::CallContext& ctx) = 0;

    /**
     * @brief 保存受限隔离载荷；同投递身份同内容幂等。
     * @param[in,out] tx 本数据库上下文的活动事务。
     * @param[in] record 投递身份、原因与受限载荷。
     * @param[in] ctx 调用期限。
     * @return 暂存结果；不触发遥测重投，提交由调用方负责。
     */
    virtual core::Status quarantine(ITransaction& tx, const QuarantineRecord& record,
                                    const core::CallContext& ctx) = 0;
};

}  // namespace ems::database

/** @file point_probe.hpp
 * @brief 显式发起候选普通点位验证，保留逐项 Data、DAR、事务及结构错误。
 */
#pragma once
#include <dlt698/service/service.hpp>
#include <dlt698/standard/capabilities.hpp>

namespace dlt698::service {
struct ProbeOptions {
    std::size_t batch_size = 16;
    standard::DeviceLayout layout;
    Limits limits;
};

struct PointResult {
    model::Oad attribute;
    std::variant<model::Data, std::uint8_t, Error> outcome;
    bool schema_checked = false;  ///< Data 是否按本地标准目录检查；厂家未知点保留原值。
    std::optional<Error> validation_error;
};

/** @brief 按协商能力顺序读取普通候选点，不自动按功能提示删除点。
 * @param[in] session 非空共享会话，调用期间被保活；须已关联，超时沿用 SessionOptions.request_timeout。
 * @param[in] capabilities 成功 CONNECT 的能力快照或未知信息，应与会话当前连接一致。
 * @param[in] attributes 非空列表；保留顺序和重复项，每个候选输出一个结果。
 * @param[in] options 最大批量、标准布局和总点数/编码资源预算。
 * @param[in] handler 非空完成回调，恰好一次；事务错误逐项保存，Data 类型错误不会丢弃原值或变成 DAR=4。
 * @throws std::invalid_argument 会话或回调为空。
 * @note 前置配置错误同步回调；已发起的操作在会话执行器中顺序回调，期间保活状态，不并发提交批次。
 * @note 已知不支持 List 时使用 Normal；已知无 Normal/List 时不发请求。未知功能信息不删除候选。
 * @note 关闭/超时后的后续批次由 Session 返回错误；不会重连或把事务失败当作点位不存在。
 */
DLT698_API void async_probe_points(std::shared_ptr<session::Session> session,
                                   standard::Capabilities capabilities,
                                   std::vector<model::Oad> attributes, ProbeOptions options,
                                   std::function<void(Result<std::vector<PointResult>>)> handler);
}  // namespace dlt698::service

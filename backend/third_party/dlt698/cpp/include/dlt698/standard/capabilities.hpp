/** @file capabilities.hpp
 * @brief CONNECT 一致性位图解释、普通读取计划和候选点能力提示。
 */
#pragma once
#include <dlt698/protocol/apdu/connection.hpp>
#include <dlt698/standard/catalog.hpp>

namespace dlt698::standard {
enum class Support { unknown, no, yes };
enum class ReadService : std::uint8_t { normal = 1, list = 2, record = 3, next = 6 };

/// 有效 CONNECT 的协商参数；空值表示尚无能力信息，不等于对端不支持。
struct Capabilities {
    std::optional<protocol::apdu::AssociationParameters> negotiated;
};

struct ReadBatch {
    bool list = false;
    std::vector<model::Oad> attributes;
};

struct CandidatePoint {
    model::Oad attribute;
    Support hint = Support::unknown;
};

/** @brief 从成功的 CONNECT 响应取得能力；非零结果不视作已连接。
 * @param[in] response 会话返回的 CONNECT 响应。
 * @return 能力快照；非零结果或缺少连接协商位返回 association_failed，并保留非零原码。
 * @note 不代替 Session 对帧尺寸、安全机制和协商子集的验证。
 */
DLT698_API Result<Capabilities> capabilities_from_connect(
    const protocol::apdu::ConnectResponse& response);
/** @brief 查询 C.1 读取服务位，按 MSB 对应序号零解释。
 * @param[in] capabilities 能力快照，未协商为 unknown。
 * @param[in] service Normal/List/Record/Next 服务。
 * @return yes/no/unknown；未知枚举为 unknown。
 */
DLT698_API Support service_support(const Capabilities& capabilities, ReadService service);
/** @brief 依据 C.2 给出标准点的业务支持提示。
 * @param[in] capabilities 能力快照；功能位全零或仅保留位时按信息未知处理。
 * @param[in] attribute 原始 OAD，保留相别/费率语义。
 * @return 业务提示，不能证明该 OAD 存在；未收录或无明确功能映射为 unknown。
 */
DLT698_API Support point_support(const Capabilities& capabilities, const model::Oad& attribute);
/** @brief 返回候选点及提示，可显式按负面业务提示筛选。
 * @param[in] capabilities 能力快照。
 * @param[in] attributes 候选列表，保留顺序和重复项。
 * @param[in] discard_negative false 保留全部，true 仅删除 hint=no 的项；unknown 始终保留。
 * @return 拥有内存的候选列表；不发起 GET，不推断厂家扩展。
 */
DLT698_API std::vector<CandidatePoint> candidate_points(const Capabilities& capabilities,
                                                        const std::vector<model::Oad>& attributes,
                                                        bool discard_negative = false);
/** @brief 规划普通读取，List 不支持时拆成单项 Normal，未知能力采用 Normal。
 * @param[in] capabilities 有效能力或未知信息。
 * @param[in] attributes 非空候选 OAD，允许厂家扩展；不验证设备是否提供该点。
 * @param[in] batch_size 最大批量数，须为 1～limits.max_elements。
 * @param[in] limits 候选总数及每批请求编码上限。
 * @return 读取批次；已知不支持 Normal/List 返回 unsupported_service，非法规模返回错误。
 */
DLT698_API Result<std::vector<ReadBatch>> plan_reads(const Capabilities& capabilities,
                                                     const std::vector<model::Oad>& attributes,
                                                     std::size_t batch_size = 16,
                                                     const Limits& limits = {});
/** @brief 检查记录服务能力，已知不支持时明确拒绝，绝不退化为普通 GET。
 * @param[in] capabilities 能力快照；未知信息由后续会话检查或设备响应决定。
 * @return 成功或 unsupported_service。
 */
DLT698_API Result<void> require_record_service(const Capabilities& capabilities);
}  // namespace dlt698::standard

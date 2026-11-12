/** @file get_block.hpp
 * @brief GET 自解析分块与链路分帧独立，以完整属性或记录行为最小单位。
 */
#pragma once
#include <dlt698/protocol/apdu/get.hpp>

namespace dlt698::protocol::apdu {
using GetSnapshot = std::variant<GetResponse, GetRecordResponse>;

class GetBlockTransfer {
   public:
    /** @brief 将拥有内存的完整快照划为有序数据块。
     * @param[in] snapshot 首次查询结果，后续取块不再次读取 provider。
     * @param[in] target_bytes 希望每个 APDU 不超过的字节数。
     * @param[in] limits 每个完整属性/记录行与 APDU 的硬上限。
     * @return 块序列或错误；不可切分单元超过 target 时由链路分帧传输，但仍须满足 limits。
     * @note 最多 65536 块，16 位块号不循环。调用方另限制快照总量、超时与会话数量。
     * @note 单次结果含重复记录 OAD 时不拆行，避免相同行列描述的不同查询混淆；收集端须禁用合并。
     */
    DLT698_API static Result<std::vector<GetNextResponse>> split(GetSnapshot snapshot,
                                                                 std::size_t target_bytes,
                                                                 const Limits& limits);
    /** @brief 开始一次有界的客户机分块收集。
     * @param[in] piid 原始调用序号及优先级。
     * @param[in] records true 收集记录结果，false 收集普通属性结果。
     * @param[in] limits 完整结果字节数和属性数量预算。
     * @param[in] merge_record_rows 是否合并相邻块中相同 OAD/RCSD 的记录行；重复 OAD 查询须为 false。
     */
    DLT698_API GetBlockTransfer(std::uint8_t piid, bool records, Limits limits,
                                bool merge_record_rows = true);
    /** @brief 接收从零开始的连续块，不接受跳号、重复或分支变化。
     * @param[in] block 一个已校验的 GET Next 响应。
     * @return 非末块返回空，末块返回完整结果；失败返回错误（DAR 原码在 remote_code）。
     * @note 不检查 TimeTag 或地址；由 Session 检查后调用，不自动重发请求。
     */
    DLT698_API Result<std::optional<GetSnapshot>> accept(const GetNextResponse& block);

   private:
    std::uint8_t piid_;
    bool records_, finished_ = false;
    bool merge_record_rows_;
    std::uint32_t next_ = 0;
    std::size_t bytes_ = 0;
    Limits limits_;
    std::vector<AttributeResult> attributes_;
    std::vector<RecordResult> records_data_;
};
}  // namespace dlt698::protocol::apdu

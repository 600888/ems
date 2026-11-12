/**
 * @file get_block_test.cpp
 * @brief GET 自解析分块的切块与收集的单元测试。
 */
#include <dlt698/protocol/apdu/get_block.hpp>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::model;
using namespace dlt698::protocol::apdu;

namespace {

/** @brief 构造一个八字节八位串属性值，用于观察切块粒度。 */
Data payload(std::uint8_t fill) { return Data{OctetString{Bytes(8, fill)}}; }

/** @brief 构造只含一个属性的 GET 响应快照。 */
GetResponse single_attribute(std::uint8_t fill) {
    GetResponse response;
    response.piid_acd = 1;
    response.attributes = {AttributeResult{Oad{0x2000, 0x02, 0x00}, payload(fill)}};
    return response;
}

/** @brief 构造指定条数的属性快照，OAD 依次递增。 */
GetResponse attribute_snapshot(std::size_t count) {
    GetResponse response;
    response.piid_acd = 1;
    for (std::size_t i = 0; i < count; ++i) {
        response.attributes.push_back(
            AttributeResult{Oad{static_cast<std::uint16_t>(0x2000 + i), 0x02, 0x00},
                            payload(static_cast<std::uint8_t>(i))});
    }
    return response;
}

/** @brief 构造单表记录快照，每行三列。 */
RecordResult record_unit(std::uint16_t oi, std::size_t rows) {
    Rcsd columns{Csd{Oad{0x2021, 0x02, 0x00}}, Csd{Oad{0x0010, 0x02, 0x00}},
                 Csd{Oad{0x0011, 0x02, 0x00}}};
    std::vector<RecordRow> data;
    for (std::size_t i = 0; i < rows; ++i) {
        data.push_back({Data{UInt32{static_cast<std::uint32_t>(i)}},
                        Data{UInt32{static_cast<std::uint32_t>(i * 2)}},
                        Data{UInt32{static_cast<std::uint32_t>(i * 3)}}});
    }
    return RecordResult{Oad{oi, 0x02, 0x00}, columns, data};
}

/**
 * @brief 校验块的编码可解码，且块号与末块标记自洽。
 * @param[in] block 待校验的块。
 */
void expect_block_is_encodable(const GetNextResponse& block) {
    auto encoded = encode_get(GetApdu{block});
    REQUIRE(static_cast<bool>(encoded));
    const Bytes bytes = encoded.value();
    auto decoded = decode_get(ByteView{bytes});
    REQUIRE(static_cast<bool>(decoded));
    const auto& back = std::get<GetNextResponse>(decoded.value());
    CHECK(back.last == block.last);
    CHECK(back.block == block.block);
    CHECK(back.piid_acd == block.piid_acd);
}

/**
 * @brief 断言块收集失败，并同时比对错误分类与上下文字段。
 * @param[in] result 待检查的结果容器。
 * @param[in] code 预期的错误分类。
 * @param[in] context 预期的诊断字段名。
 */
void require_block_error(const Result<std::optional<GetSnapshot>>& result, ErrorCode code,
                         const char* context) {
    INFO("上下文=" << context);
    REQUIRE_FALSE(static_cast<bool>(result));
    CHECK(result.error().code == code);
    CHECK(result.error().context == context);
}

}  // namespace

TEST_CASE("split 按目标字节切分属性快照", "[apdu][get][block]") {
    SECTION("小目标下每个属性单独成块") {
        // 单个属性块本身约 25 字节，因此 24 的目标必然一块一个。
        auto blocks = GetBlockTransfer::split(attribute_snapshot(5), 24, Limits{});
        REQUIRE(static_cast<bool>(blocks));
        const auto& result = blocks.value();
        REQUIRE(result.size() == 5);
        for (std::size_t i = 0; i < result.size(); ++i) {
            INFO("块下标=" << i);
            // 块号从零连续递增。
            CHECK(result[i].block == i);
            // 不可分单元不被拆散，每块只含一个完整属性。
            CHECK(std::get<std::vector<AttributeResult>>(result[i].result).size() == 1);
            // 目标只是一个愿望，实际块可以超过它而仍保持单元完整。
            auto encoded = encode_get(GetApdu{result[i]});
            REQUIRE(static_cast<bool>(encoded));
            const Bytes bytes = encoded.value();
            CHECK(bytes.size() > 24);
        }
        // 只有最后一块标记为末块。
        for (std::size_t i = 0; i + 1 < result.size(); ++i) CHECK_FALSE(result[i].last);
        CHECK(result.back().last);
    }

    SECTION("大目标下全部属性落在同一块") {
        auto blocks = GetBlockTransfer::split(attribute_snapshot(2), 1000, Limits{});
        REQUIRE(static_cast<bool>(blocks));
        const auto& result = blocks.value();
        REQUIRE(result.size() == 1);
        CHECK(result[0].block == 0);
        CHECK(result[0].last);
        CHECK(std::get<std::vector<AttributeResult>>(result[0].result).size() == 2);
    }

    SECTION("每块都能独立编解码") {
        auto blocks = GetBlockTransfer::split(attribute_snapshot(3), 24, Limits{});
        REQUIRE(static_cast<bool>(blocks));
        for (const auto& block : blocks.value()) expect_block_is_encodable(block);
    }

    SECTION("块序列可被同参数收集端完整还原") {
        auto blocks = GetBlockTransfer::split(attribute_snapshot(5), 24, Limits{});
        REQUIRE(static_cast<bool>(blocks));
        GetBlockTransfer collector{1, false, Limits{}};
        std::optional<GetSnapshot> finished;
        for (const auto& block : blocks.value()) {
            auto accepted = collector.accept(block);
            REQUIRE(static_cast<bool>(accepted));
            // 非末块不返回结果，末块才交付完整快照。
            if (accepted.value().has_value()) finished = accepted.value();
        }
        REQUIRE(finished.has_value());
        const auto& collected = std::get<GetResponse>(*finished);
        CHECK(collected.attributes.size() == 5);
        // 还原后的属性顺序与原始快照一致。
        const auto& original = attribute_snapshot(5);
        for (std::size_t i = 0; i < 5; ++i) {
            INFO("属性下标=" << i);
            CHECK(collected.attributes[i].attribute == original.attributes[i].attribute);
        }
    }

    SECTION("快照为空或目标为零被拒绝") {
        auto empty = GetBlockTransfer::split(GetResponse{1, false, {}, {}}, 100, Limits{});
        REQUIRE_FALSE(static_cast<bool>(empty));
        CHECK(empty.error().code == ErrorCode::resource_limit);
        CHECK(empty.error().context == "GET snapshot units");

        GetResponse response;
        response.piid_acd = 1;
        response.attributes = {AttributeResult{Oad{0x2000, 0x02, 0x00}, Null{}}};
        auto zero = GetBlockTransfer::split(response, 0, Limits{});
        REQUIRE_FALSE(static_cast<bool>(zero));
        CHECK(zero.error().code == ErrorCode::invalid_value);
        CHECK(zero.error().context == "GET block target");
    }

    SECTION("单个不可分单元超过硬上限时报资源超限") {
        // limits 是硬约束，即使 target 允许更大也不得放行。
        const Data inner{Array{{Data{UInt32{1}}, Data{UInt32{2}}, Data{UInt32{3}}}}};
        GetResponse response;
        response.piid_acd = 1;
        response.attributes = {AttributeResult{Oad{0x2000, 0x02, 0x00}, inner}};
        Limits limits;
        limits.max_data_bytes = 12;
        auto blocks = GetBlockTransfer::split(response, 1000, limits);
        REQUIRE_FALSE(static_cast<bool>(blocks));
        CHECK(blocks.error().code == ErrorCode::resource_limit);
    }

    SECTION("单元数超出上限被拒绝") {
        GetResponse response;
        response.piid_acd = 1;
        for (std::size_t i = 0; i < 3; ++i)
            response.attributes.push_back(
                AttributeResult{Oad{static_cast<std::uint16_t>(0x2000 + i), 0x02, 0x00}, Null{}});
        Limits limits;
        limits.max_elements = 2;
        auto blocks = GetBlockTransfer::split(response, 1000, limits);
        REQUIRE_FALSE(static_cast<bool>(blocks));
        CHECK(blocks.error().code == ErrorCode::resource_limit);
        CHECK(blocks.error().context == "GET snapshot units");
    }
}

TEST_CASE("split 以行为最小单位切分记录快照", "[apdu][get][block][record]") {
    SECTION("单表多行按行边界拆分且不拆散单行") {
        GetRecordResponse snapshot;
        snapshot.piid_acd = 1;
        snapshot.records = {record_unit(0x5004, 5)};
        auto blocks = GetBlockTransfer::split(snapshot, 45, Limits{});
        REQUIRE(static_cast<bool>(blocks));
        const auto& result = blocks.value();
        REQUIRE(result.size() == 5);
        for (std::size_t i = 0; i < result.size(); ++i) {
            INFO("块下标=" << i);
            const auto& records = std::get<std::vector<RecordResult>>(result[i].result);
            REQUIRE(records.size() == 1);
            // 每块只带一行，行内三列必须完整。
            CHECK(std::get<std::vector<RecordRow>>(records[0].result).size() == 1);
            CHECK(std::get<std::vector<RecordRow>>(records[0].result)[0].size() == 3);
            // 每块重复精确的 OAD 与 RCSD 表头。
            CHECK(records[0].attribute.oi == 0x5004);
            CHECK(records[0].columns.size() == 3);
            CHECK(result[i].block == i);
        }
        CHECK(result.back().last);
    }

    SECTION("大目标下整表落在同一块") {
        GetRecordResponse snapshot;
        snapshot.piid_acd = 1;
        snapshot.records = {record_unit(0x5004, 3)};
        auto blocks = GetBlockTransfer::split(snapshot, 1000, Limits{});
        REQUIRE(static_cast<bool>(blocks));
        const auto& result = blocks.value();
        REQUIRE(result.size() == 1);
        const auto& records = std::get<std::vector<RecordResult>>(result[0].result);
        REQUIRE(records.size() == 1);
        CHECK(std::get<std::vector<RecordRow>>(records[0].result).size() == 3);
    }

    SECTION("多表快照按表顺序切分") {
        GetRecordResponse snapshot;
        snapshot.piid_acd = 1;
        snapshot.records = {record_unit(0x5004, 1), record_unit(0x5005, 1)};
        auto blocks = GetBlockTransfer::split(snapshot, 1000, Limits{});
        REQUIRE(static_cast<bool>(blocks));
        const auto& result = blocks.value();
        REQUIRE(result.size() == 1);
        const auto& records = std::get<std::vector<RecordResult>>(result[0].result);
        REQUIRE(records.size() == 2);
        // 表顺序不变。
        CHECK(records[0].attribute.oi == 0x5004);
        CHECK(records[1].attribute.oi == 0x5005);
    }

    SECTION("重复OAD 的多表快照不拆行") {
        // 相同 OAD 的两次查询不能混在同一张表里，因此整单元交付而不是按行拆。
        GetRecordResponse snapshot;
        snapshot.piid_acd = 1;
        snapshot.records = {record_unit(0x5004, 2), record_unit(0x5004, 2)};
        auto blocks = GetBlockTransfer::split(snapshot, 40, Limits{});
        REQUIRE(static_cast<bool>(blocks));
        const auto& result = blocks.value();
        for (const auto& block : result) {
            const auto& records = std::get<std::vector<RecordResult>>(block.result);
            for (const auto& record : records) {
                const auto& rows = std::get<std::vector<RecordRow>>(record.result);
                // 每次查询的两行始终在同一块里。
                CHECK(rows.size() == 2);
            }
        }
        // 收集端必须禁用合并，否则两个相同 OAD 的记录会被错误拼接。
        GetBlockTransfer collector{1, true, Limits{}, false};
        std::optional<GetSnapshot> finished;
        for (const auto& block : result) {
            auto accepted = collector.accept(block);
            REQUIRE(static_cast<bool>(accepted));
            if (accepted.value().has_value()) finished = accepted.value();
        }
        REQUIRE(finished.has_value());
        const auto& collected = std::get<GetRecordResponse>(*finished);
        CHECK(collected.records.size() == 2);
    }
}

TEST_CASE("accept 校验 PIID 与块号连续性", "[apdu][get][block]") {
    GetNextResponse first{
        1, false, 0, std::vector<AttributeResult>{{Oad{0x4001, 2, 0}, Null{}}}, {}};
    GetNextResponse second{
        1, true, 1, std::vector<AttributeResult>{{Oad{0x4002, 2, 0}, Null{}}}, {}};

    SECTION("从零开始的连续块被接受") {
        GetBlockTransfer collector{1, false, Limits{}};
        auto first_result = collector.accept(first);
        REQUIRE(static_cast<bool>(first_result));
        // 非末块返回空，不交付半成品。
        CHECK_FALSE(first_result.value().has_value());
        auto second_result = collector.accept(second);
        REQUIRE(static_cast<bool>(second_result));
        REQUIRE(second_result.value().has_value());
        const auto& collected = std::get<GetResponse>(*second_result.value());
        CHECK(collected.attributes.size() == 2);
    }

    SECTION("PIID 不匹配被拒绝") {
        GetBlockTransfer collector{1, false, Limits{}};
        GetNextResponse other{
            2, false, 0, std::vector<AttributeResult>{{Oad{0x4001, 2, 0}, Null{}}}, {}};
        auto result = collector.accept(other);
        require_block_error(result, ErrorCode::invalid_value, "GET block PIID/order");
    }

    SECTION("ACD 标志位不参与 PIID 匹配") {
        // bit6 是服务器主动上报标志，分块比较时必须屏蔽，否则会误判为不同调用。
        GetBlockTransfer collector{0x01, false, Limits{}};
        GetNextResponse flagged{
            0x41, true, 0, std::vector<AttributeResult>{{Oad{0x4001, 2, 0}, Null{}}}, {}};
        auto result = collector.accept(flagged);
        REQUIRE(static_cast<bool>(result));
        REQUIRE(result.value().has_value());
    }

    SECTION("ACD 之外的位变化仍视为不匹配") {
        GetBlockTransfer collector{0x01, false, Limits{}};
        GetNextResponse high_bit{
            0x81, true, 0, std::vector<AttributeResult>{{Oad{0x4001, 2, 0}, Null{}}}, {}};
        auto result = collector.accept(high_bit);
        require_block_error(result, ErrorCode::invalid_value, "GET block PIID/order");
    }

    SECTION("跳过块号被拒绝且不推进") {
        GetBlockTransfer collector{1, false, Limits{}};
        auto skipped = collector.accept(second);
        require_block_error(skipped, ErrorCode::invalid_value, "GET block PIID/order");
        // 期望块号仍是零，重新提交首块必须成功。
        auto retried = collector.accept(first);
        REQUIRE(static_cast<bool>(retried));
    }

    SECTION("重复块号被拒绝") {
        GetBlockTransfer collector{1, false, Limits{}};
        REQUIRE(static_cast<bool>(collector.accept(first)));
        auto repeated = collector.accept(first);
        require_block_error(repeated, ErrorCode::invalid_value, "GET block PIID/order");
    }

    SECTION("末块之后再收任何块都被拒绝") {
        GetBlockTransfer collector{1, false, Limits{}};
        REQUIRE(static_cast<bool>(collector.accept(first)));
        REQUIRE(static_cast<bool>(collector.accept(second)));
        // 末块已交付完整快照，本地不得再接纳新块。
        auto after = collector.accept(first);
        require_block_error(after, ErrorCode::invalid_value, "GET block PIID/order");
    }

    SECTION("结果分支与收集器类型不匹配被拒绝") {
        GetBlockTransfer collector{1, false, Limits{}};
        GetNextResponse records{
            1, true, 0, std::vector<RecordResult>{{Oad{0x5004, 2, 0}, {}, std::uint8_t{0}}}, {}};
        auto result = collector.accept(records);
        require_block_error(result, ErrorCode::invalid_value, "GET block result kind");

        GetBlockTransfer record_collector{1, true, Limits{}};
        GetNextResponse attributes{
            1, true, 0, std::vector<AttributeResult>{{Oad{0x4001, 2, 0}, Null{}}}, {}};
        auto mismatch = record_collector.accept(attributes);
        require_block_error(mismatch, ErrorCode::invalid_value, "GET block result kind");
    }

    SECTION("空结果列表被拒绝") {
        GetBlockTransfer collector{1, false, Limits{}};
        GetNextResponse empty{1, true, 0, std::vector<AttributeResult>{}, {}};
        auto result = collector.accept(empty);
        REQUIRE_FALSE(static_cast<bool>(result));
    }
}

TEST_CASE("结果 DAR 转为远端错误并保留原码", "[apdu][get][block]") {
    SECTION("末块 DAR 转成 remote_error") {
        GetBlockTransfer collector{1, false, Limits{}};
        GetNextResponse dar{1, true, 0, std::uint8_t{250}, {}};
        auto result = collector.accept(dar);
        REQUIRE_FALSE(static_cast<bool>(result));
        CHECK(result.error().code == ErrorCode::remote_error);
        CHECK(result.error().context == "GET block DAR");
        // 远端码必须原样带给调用方，不能被替换成宿主错误码。
        REQUIRE(result.error().remote_code.has_value());
        CHECK(result.error().remote_code.value() == 250);
    }

    SECTION("DAR 原码覆盖 0 至 255") {
        for (int dar = 0; dar <= 255; ++dar) {
            INFO("DAR=" << dar);
            GetBlockTransfer collector{1, false, Limits{}};
            GetNextResponse block{1, true, 0, std::uint8_t{static_cast<std::uint8_t>(dar)}, {}};
            auto result = collector.accept(block);
            REQUIRE_FALSE(static_cast<bool>(result));
            CHECK(result.error().code == ErrorCode::remote_error);
            REQUIRE(result.error().remote_code.has_value());
            CHECK(result.error().remote_code.value() == dar);
        }
    }

    SECTION("记录收集器同样识别 DAR") {
        GetBlockTransfer collector{1, true, Limits{}};
        GetNextResponse dar{1, true, 0, std::uint8_t{3}, {}};
        auto result = collector.accept(dar);
        REQUIRE_FALSE(static_cast<bool>(result));
        CHECK(result.error().code == ErrorCode::remote_error);
        REQUIRE(result.error().remote_code.has_value());
        CHECK(result.error().remote_code.value() == 3);
    }

    SECTION("DAR 之后收集器不再接受后续块") {
        GetBlockTransfer collector{1, false, Limits{}};
        auto dar = collector.accept(GetNextResponse{1, true, 0, std::uint8_t{250}, {}});
        REQUIRE_FALSE(static_cast<bool>(dar));
        // 远端已明确失败，本地不得再把后续块当作正常流程。
        auto after = collector.accept(GetNextResponse{1, true, 1, std::uint8_t{0}, {}});
        require_block_error(after, ErrorCode::invalid_value, "GET block PIID/order");
    }
}

TEST_CASE("accept 累计字节与单元数上限", "[apdu][get][block]") {
    SECTION("累计属性数超出上限被拒绝") {
        Limits limits;
        limits.max_elements = 3;
        GetBlockTransfer collector{1, false, limits};
        // 首块两项合法。
        GetNextResponse first{
            1,
            false,
            0,
            std::vector<AttributeResult>{{Oad{0x4001, 2, 0}, Null{}}, {Oad{0x4002, 2, 0}, Null{}}},
            {}};
        REQUIRE(static_cast<bool>(collector.accept(first)));
        // 第二块再要两项，累计四项超出上限。
        GetNextResponse second{
            1,
            true,
            1,
            std::vector<AttributeResult>{{Oad{0x4003, 2, 0}, Null{}}, {Oad{0x4004, 2, 0}, Null{}}},
            {}};
        auto result = collector.accept(second);
        require_block_error(result, ErrorCode::resource_limit, "GET aggregate units");
    }

    SECTION("累计恰好等于上限时放行") {
        Limits limits;
        limits.max_elements = 3;
        GetBlockTransfer collector{1, false, limits};
        GetNextResponse block{1,
                              true,
                              0,
                              std::vector<AttributeResult>{{Oad{0x4001, 2, 0}, Null{}},
                                                           {Oad{0x4002, 2, 0}, Null{}},
                                                           {Oad{0x4003, 2, 0}, Null{}}},
                              {}};
        auto result = collector.accept(block);
        REQUIRE(static_cast<bool>(result));
        REQUIRE(result.value().has_value());
        CHECK(std::get<GetResponse>(*result.value()).attributes.size() == 3);
    }

    SECTION("单块编码已超硬上限时被拒绝") {
        Limits limits;
        limits.max_data_bytes = 8;
        GetBlockTransfer collector{1, false, limits};
        GetNextResponse block{
            1, true, 0, std::vector<AttributeResult>{{Oad{0x4001, 2, 0}, Null{}}}, {}};
        auto result = collector.accept(block);
        REQUIRE_FALSE(static_cast<bool>(result));
        CHECK(result.error().code == ErrorCode::resource_limit);
    }

    SECTION("累计传输字节超出上限被拒绝") {
        // 每块编码约 25 字节，上限 40 允许一块但不允许两块。
        Limits limits;
        limits.max_data_bytes = 40;
        GetBlockTransfer collector{1, false, limits};
        GetNextResponse first{
            1, false, 0, std::vector<AttributeResult>{{Oad{0x4001, 2, 0}, Null{}}}, {}};
        auto first_result = collector.accept(first);
        REQUIRE(static_cast<bool>(first_result));
        // 收满两块后必须停止，而不是无限累积直到宿主内存耗尽。
        GetBlockTransfer smaller{1, false, Limits{}};
        Limits tight;
        // 用首块的实际编码长度构造"恰好一块"的预算。
        auto encoded = encode_get(GetApdu{first});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes first_bytes = encoded.value();
        tight.max_data_bytes = first_bytes.size();
        GetBlockTransfer bounded{1, false, tight};
        REQUIRE(static_cast<bool>(bounded.accept(first)));
        GetNextResponse second{
            1, true, 1, std::vector<AttributeResult>{{Oad{0x4002, 2, 0}, Null{}}}, {}};
        auto second_result = bounded.accept(second);
        require_block_error(second_result, ErrorCode::resource_limit, "GET aggregate bytes");
        (void)smaller;
    }

    SECTION("行列乘积超出元素上限被拒绝") {
        Limits limits;
        limits.max_elements = 2;
        GetBlockTransfer collector{1, true, limits};
        // 三列乘两行共六格，超出上限。
        GetNextResponse block{
            1,
            false,
            0,
            std::vector<RecordResult>{RecordResult{
                Oad{0x5004, 2, 0}, Rcsd{Csd{Oad{0x2021, 2, 0}}, Csd{Oad{0x0010, 2, 0}}},
                std::vector<RecordRow>{{Data{UInt32{1}}, Data{UInt32{2}}},
                                       {Data{UInt32{3}}, Data{UInt32{4}}},
                                       {Data{UInt32{5}}, Data{UInt32{6}}}}}},
            {}};
        auto result = collector.accept(block);
        REQUIRE_FALSE(static_cast<bool>(result));
        CHECK(result.error().code == ErrorCode::resource_limit);
    }

    SECTION("块号耗尽后不再接受更多块") {
        // 直接构造 65535 号块：收集器期望零号，因此按顺序错误拒绝。
        GetBlockTransfer collector{1, false, Limits{}};
        GetNextResponse exhausted{
            1, true, 65535, std::vector<AttributeResult>{{Oad{0x4001, 2, 0}, Null{}}}, {}};
        auto result = collector.accept(exhausted);
        require_block_error(result, ErrorCode::invalid_value, "GET block PIID/order");
    }
}

TEST_CASE("merge_record_rows 决定相邻同表记录行的合并", "[apdu][get][block][record]") {
    // 两块各带同一张表的一部分行，用于验证合并开关。
    auto build_blocks = [] {
        GetRecordResponse snapshot;
        snapshot.piid_acd = 1;
        snapshot.records = {record_unit(0x5004, 2)};
        auto blocks = GetBlockTransfer::split(snapshot, 26, Limits{});
        return blocks;
    };

    SECTION("默认合并相邻同 OAD 与同 RCSD 的行") {
        auto blocks = build_blocks();
        REQUIRE(static_cast<bool>(blocks));
        REQUIRE(blocks.value().size() == 2);
        GetBlockTransfer collector{1, true, Limits{}, true};
        std::optional<GetSnapshot> finished;
        for (const auto& block : blocks.value()) {
            auto accepted = collector.accept(block);
            REQUIRE(static_cast<bool>(accepted));
            if (accepted.value().has_value()) finished = accepted.value();
        }
        REQUIRE(finished.has_value());
        const auto& collected = std::get<GetRecordResponse>(*finished);
        // 合并后只剩一张表，两行都在其中。
        REQUIRE(collected.records.size() == 1);
        CHECK(collected.records[0].attribute.oi == 0x5004);
        CHECK(std::get<std::vector<RecordRow>>(collected.records[0].result).size() == 2);
        CHECK(collected.records[0].columns.size() == 3);
    }

    SECTION("关闭合并时保持原有记录顺序") {
        auto blocks = build_blocks();
        REQUIRE(static_cast<bool>(blocks));
        GetBlockTransfer collector{1, true, Limits{}, false};
        std::optional<GetSnapshot> finished;
        for (const auto& block : blocks.value()) {
            auto accepted = collector.accept(block);
            REQUIRE(static_cast<bool>(accepted));
            if (accepted.value().has_value()) finished = accepted.value();
        }
        REQUIRE(finished.has_value());
        const auto& collected = std::get<GetRecordResponse>(*finished);
        // 不合并时每块一张表，行数与块数一致。
        REQUIRE(collected.records.size() == 2);
        for (const auto& record : collected.records) {
            CHECK(record.attribute.oi == 0x5004);
            CHECK(std::get<std::vector<RecordRow>>(record.result).size() == 1);
        }
    }

    SECTION("合并时列头不一致必须拒绝") {
        GetBlockTransfer collector{1, true, Limits{}, true};
        Rcsd first_columns{Csd{Oad{0x2021, 2, 0}}};
        Rcsd other_columns{Csd{Oad{0x0010, 2, 0}}};
        GetNextResponse first{
            1,
            false,
            0,
            std::vector<RecordResult>{
                {Oad{0x5004, 2, 0}, first_columns, std::vector<RecordRow>{{Data{UInt32{1}}}}}},
            {}};
        REQUIRE(static_cast<bool>(collector.accept(first)));
        // 相同 OAD 但列头不同，说明不是同一次查询的延续。
        GetNextResponse second{
            1,
            true,
            1,
            std::vector<RecordResult>{
                {Oad{0x5004, 2, 0}, other_columns, std::vector<RecordRow>{{Data{UInt32{2}}}}}},
            {}};
        auto result = collector.accept(second);
        require_block_error(result, ErrorCode::invalid_value,
                            "GET record continuation header/result");
    }

    SECTION("合并时后续块携带 DAR 也不是延续") {
        GetBlockTransfer collector{1, true, Limits{}, true};
        GetNextResponse first{
            1,
            false,
            0,
            std::vector<RecordResult>{{Oad{0x5004, 2, 0},
                                       {Csd{Oad{0x2021, 2, 0}}},
                                       std::vector<RecordRow>{{Data{UInt32{1}}}}}},
            {}};
        REQUIRE(static_cast<bool>(collector.accept(first)));
        GetNextResponse dar{1, true, 1, std::uint8_t{1}, {}};
        auto result = collector.accept(dar);
        // DAR 优先判定为远端错误，而不是列头不一致。
        REQUIRE_FALSE(static_cast<bool>(result));
        CHECK(result.error().code == ErrorCode::remote_error);
    }

    SECTION("属性快照不受合并开关影响") {
        auto blocks = GetBlockTransfer::split(attribute_snapshot(3), 24, Limits{});
        REQUIRE(static_cast<bool>(blocks));
        // records=false 的收集器对普通属性没有合并概念。
        GetBlockTransfer collector{1, false, Limits{}, true};
        std::optional<GetSnapshot> finished;
        for (const auto& block : blocks.value()) {
            auto accepted = collector.accept(block);
            REQUIRE(static_cast<bool>(accepted));
            if (accepted.value().has_value()) finished = accepted.value();
        }
        REQUIRE(finished.has_value());
        CHECK(std::get<GetResponse>(*finished).attributes.size() == 3);
    }
}

TEST_CASE("GET 分块的最终跟随上报随完整快照保留", "[apdu][get][block][follow]") {
    auto snapshot = attribute_snapshot(5);
    snapshot.follow_report =
        FollowReport{std::vector<AttributeResult>{{{0x3000, 2, 0}, std::uint8_t{4}}}};
    const auto blocks = GetBlockTransfer::split(snapshot, 24, Limits{});
    REQUIRE(blocks);
    REQUIRE(blocks.value().size() > 1);
    GetBlockTransfer collector(snapshot.piid_acd, false, Limits{});
    for (std::size_t i = 0; i < blocks.value().size(); ++i) {
        const auto& block = blocks.value()[i];
        CHECK(block.follow_report.has_value() == block.last);
        auto result = collector.accept(block);
        REQUIRE(result);
        if (!block.last)
            CHECK_FALSE(result.value());
        else {
            REQUIRE(result.value());
            REQUIRE(std::get<GetResponse>(*result.value()).follow_report);
            CHECK(std::get<GetResponse>(*result.value()).attributes.size() == 5);
        }
    }
}

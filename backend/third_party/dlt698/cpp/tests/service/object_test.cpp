/**
 * @file object_test.cpp
 * @brief 对象目录 schema 校验、权限与类型检查及内存模拟对象的单元测试。
 */
#include <dlt698/service/object.hpp>
#include <dlt698/service/standard_object.hpp>
#include <stdexcept>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::service;
namespace apdu = protocol::apdu;
using apdu::RecordResult;
using apdu::RecordRow;

namespace {

/// 断言读取结果是 DAR 并返回原码。
std::uint8_t dar_of(const ObjectValue& value) {
    REQUIRE(std::holds_alternative<std::uint8_t>(value));
    return std::get<std::uint8_t>(value);
}

/// 断言读取结果是 Data 并返回其协议类型。
model::DataType data_type_of(const ObjectValue& value) {
    REQUIRE(std::holds_alternative<model::Data>(value));
    return std::get<model::Data>(value).type();
}

/// 断言记录结果是 DAR 并返回原码。
std::uint8_t record_dar_of(const RecordResult& result) {
    REQUIRE(std::holds_alternative<std::uint8_t>(result.result));
    return std::get<std::uint8_t>(result.result);
}

/// 构造仅实现 read 的最小 provider，用于验证接口缺省实现保持只读拒绝。
struct ReadOnlyProvider final : IObjectProvider {
    ObjectValue read(const model::Oad& attribute) override {
        return attribute.index ? ObjectValue{std::uint8_t{8}}
                               : ObjectValue{model::Data{model::UInt16{
                                     static_cast<std::uint16_t>(attribute.attribute)}}};
    }
};

/// 可编程 provider，按字段决定返回、拒绝或抛异常。
class ScriptedProvider final : public IObjectProvider {
   public:
    ObjectValue value{model::Data{model::UInt16{1}}};
    std::uint8_t dar = 0;
    bool explode = false;
    std::optional<RecordResult> record;
    ActionValue action{0, {}};
    unsigned reads = 0;

    ObjectValue read(const model::Oad&) override {
        if (explode) throw std::runtime_error("provider read failure");
        ++reads;
        return value;
    }

    std::uint8_t write(const model::Oad&, const model::Data&) override {
        if (explode) throw std::runtime_error("provider write failure");
        return dar;
    }

    ActionValue invoke(const model::Omd&, const model::Data&) override {
        if (explode) throw std::runtime_error("provider invoke failure");
        return action;
    }

    RecordResult read_record(const apdu::GetRecord& query) override {
        if (explode) throw std::runtime_error("provider record failure");
        return record ? *record : RecordResult{query.attribute, query.columns, std::uint8_t{3}};
    }
};

/// 目录锁外调用的可观测证据：provider 在 read 中重入目录读取另一个对象。
struct ReentrantProvider final : IObjectProvider {
    const ObjectRegistry* registry = nullptr;
    model::Oad other{0, 0, 0};
    ObjectValue observed;
    bool reentered = false;

    ObjectValue read(const model::Oad&) override {
        // 若目录在持锁状态下调用 provider，这里的重入会死锁。
        if (registry && !reentered) {
            reentered = true;
            observed = registry->read(other);
        }
        return model::Data{model::UInt16{7}};
    }
};

/// 只有方法没有属性的合法 schema，用于验证方法编号校验。
ObjectSchema method_only(std::uint16_t oi, std::vector<MethodSchema> methods) {
    return ObjectSchema{oi, "method-only", {}, std::move(methods)};
}

}  // namespace

TEST_CASE("对象目录按 schema 校验属性与方法定义", "[service][object]") {
    ObjectRegistry registry;
    auto provider = std::make_shared<ReadOnlyProvider>();

    SECTION("合法 schema 注册成功") {
        ObjectSchema schema{0x2000,
                            "电压",
                            {{1, model::DataType::octet_string, true, false, false},
                             {2, model::DataType::array, true, false, false}},
                            {}};
        REQUIRE(static_cast<bool>(registry.register_object(schema, provider)));
        // 至少定义一项：纯方法对象同样合法。
        REQUIRE(static_cast<bool>(registry.register_object(
            method_only(0x2001, {{1, model::DataType::uint8, model::DataType::null, true}}),
            provider)));
    }

    SECTION("provider 为空被拒绝") {
        auto result = registry.register_object(
            ObjectSchema{0x2000, "x", {{2, model::DataType::null}}, {}}, nullptr);
        test::require_error(result, ErrorCode::invalid_value);
        CHECK(result.error().context == "object schema/provider");
    }

    SECTION("既无属性也无方法被拒绝") {
        auto result = registry.register_object(ObjectSchema{0x2000, "x", {}, {}}, provider);
        test::require_error(result, ErrorCode::invalid_value);
        CHECK(result.error().context == "object schema/provider");
    }

    SECTION("属性数量超过 31 被拒绝") {
        ObjectSchema schema{0x2000, "x", {}, {}};
        for (std::uint8_t n = 1; n <= 32; ++n)
            schema.attributes.push_back({n, model::DataType::null});
        auto result = registry.register_object(schema, provider);
        test::require_error(result, ErrorCode::invalid_value);
        CHECK(result.error().context == "object schema/provider");
    }

    SECTION("属性编号零与 32 都被拒绝，31 合法") {
        auto zero = registry.register_object(
            ObjectSchema{0x2000, "x", {{0, model::DataType::null}}, {}}, provider);
        test::require_error(zero, ErrorCode::invalid_value);
        CHECK(zero.error().context == "attribute schema");
        auto high = registry.register_object(
            ObjectSchema{0x2000, "x", {{32, model::DataType::null}}, {}}, provider);
        test::require_error(high, ErrorCode::invalid_value);
        CHECK(high.error().context == "attribute schema");
        REQUIRE(static_cast<bool>(registry.register_object(
            ObjectSchema{0x2000, "x", {{31, model::DataType::null}}, {}}, provider)));
    }

    SECTION("重复属性编号被拒绝") {
        auto result = registry.register_object(
            ObjectSchema{0x2000, "x", {{2, model::DataType::null}, {2, model::DataType::null}}, {}},
            provider);
        test::require_error(result, ErrorCode::invalid_value);
        CHECK(result.error().context == "attribute schema");
    }

    SECTION("方法编号零被拒绝而 255 合法，重复编号同样被拒绝") {
        auto zero = registry.register_object(method_only(0x2000, {{0, {}, {}, true}}), provider);
        test::require_error(zero, ErrorCode::invalid_value);
        CHECK(zero.error().context == "method schema");
        REQUIRE(static_cast<bool>(
            registry.register_object(method_only(0x2000, {{255, {}, {}, true}}), provider)));
        auto duplicate = registry.register_object(
            method_only(0x2001, {{3, {}, {}, true}, {3, {}, {}, true}}), provider);
        test::require_error(duplicate, ErrorCode::invalid_value);
        CHECK(duplicate.error().context == "method schema");
    }

    SECTION("同一 OI 重复注册被拒绝且不覆盖已有 provider") {
        auto first = std::make_shared<ScriptedProvider>();
        first->value = model::Data{model::UInt16{11}};
        REQUIRE(static_cast<bool>(registry.register_object(
            ObjectSchema{0x2000, "x", {{2, model::DataType::uint16}}, {}}, first)));
        auto second = std::make_shared<ScriptedProvider>();
        second->value = model::Data{model::UInt16{22}};
        auto result = registry.register_object(
            ObjectSchema{0x2000, "y", {{2, model::DataType::uint16}}, {}}, second);
        test::require_error(result, ErrorCode::busy);
        CHECK(result.error().context == "duplicate OI");
        // 原 provider 仍然生效，重复注册没有替换目录项。
        auto value = registry.read({0x2000, 2, 0});
        REQUIRE(std::holds_alternative<model::Data>(value));
        CHECK(std::get<model::Data>(value) == model::Data{model::UInt16{11}});
        CHECK(first->reads == 1);
        CHECK(second->reads == 0);
    }
}

TEST_CASE("对象目录按读权限与类型返回 DAR", "[service][object]") {
    ObjectRegistry registry;
    auto provider = std::make_shared<ScriptedProvider>();
    provider->value = model::Data{model::UInt16{220}};

    SECTION("未注册 OI 与未定义属性都是 DAR=4") {
        REQUIRE(static_cast<bool>(registry.register_object(
            ObjectSchema{0x2000, "x", {{2, model::DataType::uint16}}, {}}, provider)));
        CHECK(dar_of(registry.read({0x9999, 2, 0})) == 4);
        CHECK(dar_of(registry.read({0x2000, 3, 0})) == 4);
        // 特征位不影响属性编号匹配，schema 按低五位查找。
        CHECK(data_type_of(registry.read({0x2000, 0x82, 0})) == model::DataType::uint16);
    }

    SECTION("不可读属性返回 DAR=3 且不调用 provider") {
        REQUIRE(static_cast<bool>(registry.register_object(
            ObjectSchema{0x2000, "x", {{2, model::DataType::uint16, false, false, false}}, {}},
            provider)));
        CHECK(dar_of(registry.read({0x2000, 2, 0})) == 3);
        CHECK(provider->reads == 0);
    }

    SECTION("记录型属性不能按普通属性读取") {
        REQUIRE(static_cast<bool>(registry.register_object(
            ObjectSchema{0x5004, "x", {{2, model::DataType::array, true, false, true}}, {}},
            provider)));
        CHECK(dar_of(registry.read({0x5004, 2, 0})) == 3);
    }

    SECTION("整体读取的类型不符返回 DAR=7，索引读取交由 provider 解释") {
        REQUIRE(static_cast<bool>(registry.register_object(
            ObjectSchema{0x2000, "x", {{2, model::DataType::uint16}}, {}}, provider)));
        provider->value = model::Data{model::UInt8{5}};
        CHECK(dar_of(registry.read({0x2000, 2, 0})) == 7);
        // 索引非零时目录不做整体类型判断，元素类型是 provider 的责任。
        CHECK(data_type_of(registry.read({0x2000, 2, 1})) == model::DataType::uint8);
    }

    SECTION("provider 返回的 DAR 原样透传") {
        REQUIRE(static_cast<bool>(registry.register_object(
            ObjectSchema{0x2000, "x", {{2, model::DataType::uint16}}, {}}, provider)));
        provider->value = std::uint8_t{6};
        CHECK(dar_of(registry.read({0x2000, 2, 0})) == 6);
    }

    SECTION("provider 抛异常被隔离为 DAR=255") {
        REQUIRE(static_cast<bool>(registry.register_object(
            ObjectSchema{0x2000, "x", {{2, model::DataType::uint16}}, {}}, provider)));
        provider->explode = true;
        CHECK(dar_of(registry.read({0x2000, 2, 0})) == 255);
    }

    SECTION("provider 在目录锁外被调用，可重入目录读取其他对象") {
        auto reentrant = std::make_shared<ReentrantProvider>();
        REQUIRE(static_cast<bool>(registry.register_object(
            ObjectSchema{0x2000, "x", {{2, model::DataType::uint16}}, {}}, reentrant)));
        REQUIRE(static_cast<bool>(registry.register_object(
            ObjectSchema{0x2001, "y", {{2, model::DataType::uint16}}, {}}, provider)));
        reentrant->registry = &registry;
        reentrant->other = model::Oad{0x2001, 2, 0};
        provider->value = model::Data{model::UInt16{33}};
        CHECK(data_type_of(registry.read({0x2000, 2, 0})) == model::DataType::uint16);
        REQUIRE(reentrant->reentered);
        // 重入读到的是另一个对象的真实值，说明没有自锁。
        REQUIRE(std::holds_alternative<model::Data>(reentrant->observed));
        CHECK(std::get<model::Data>(reentrant->observed) == model::Data{model::UInt16{33}});
    }
}

TEST_CASE("对象目录的写权限默认关闭", "[service][object]") {
    ObjectRegistry registry;
    auto provider = std::make_shared<ScriptedProvider>();
    provider->dar = 0;
    // writable 保持默认值 false：可读不等于可写。
    REQUIRE(static_cast<bool>(registry.register_object(
        ObjectSchema{0x2000, "x", {{2, model::DataType::uint16, true, false, false}}, {}},
        provider)));
    REQUIRE(static_cast<bool>(registry.register_object(
        ObjectSchema{0x2001, "y", {{2, model::DataType::uint16, true, true, false}}, {}},
        provider)));

    SECTION("读取 schema 不会自动获得写权限") {
        CHECK(registry.write({0x2000, 2, 0}, model::Data{model::UInt16{1}}) == 3);
    }

    SECTION("显式可写属性提交给 provider，provider 的 DAR 原样返回") {
        CHECK(registry.write({0x2001, 2, 0}, model::Data{model::UInt16{1}}) == 0);
        provider->dar = 9;
        CHECK(registry.write({0x2001, 2, 0}, model::Data{model::UInt16{1}}) == 9);
    }

    SECTION("整体类型不符在调用 provider 前返回 DAR=7") {
        provider->dar = 0;
        CHECK(registry.write({0x2001, 2, 0}, model::Data{model::UInt8{1}}) == 7);
        // 索引非零时不判断整体类型。
        CHECK(registry.write({0x2001, 2, 1}, model::Data{model::UInt8{1}}) == 0);
    }

    SECTION("未注册与未定义属性返回 DAR=4") {
        CHECK(registry.write({0x9999, 2, 0}, model::Data{model::UInt16{1}}) == 4);
        CHECK(registry.write({0x2001, 3, 0}, model::Data{model::UInt16{1}}) == 4);
    }

    SECTION("记录型属性不可写") {
        REQUIRE(static_cast<bool>(registry.register_object(
            ObjectSchema{0x5004, "z", {{2, model::DataType::array, true, true, true}}, {}},
            provider)));
        CHECK(registry.write({0x5004, 2, 0}, model::Data{model::Array{}}) == 3);
    }

    SECTION("provider 抛异常被隔离为 DAR=255") {
        provider->explode = true;
        CHECK(registry.write({0x2001, 2, 0}, model::Data{model::UInt16{1}}) == 255);
    }
}

TEST_CASE("对象目录的方法与记录分发", "[service][object]") {
    ObjectRegistry registry;
    auto provider = std::make_shared<ScriptedProvider>();
    REQUIRE(static_cast<bool>(registry.register_object(
        ObjectSchema{0x4001,
                     "x",
                     {{2, model::DataType::array, true, false, true},
                      {3, model::DataType::uint16, true, false, false}},
                     {{1, model::DataType::uint8, model::DataType::uint16, true},
                      {2, model::DataType::uint8, std::nullopt, false},
                      {3, std::nullopt, std::nullopt, true}}},
        provider)));

    SECTION("方法未定义返回 DAR=4，未注册 OI 同样是 4") {
        CHECK(registry.invoke({0x4001, 9, 0}, model::Data{model::UInt8{1}}).dar == 4);
        CHECK(registry.invoke({0x9999, 1, 0}, model::Data{model::UInt8{1}}).dar == 4);
    }

    SECTION("不可执行方法返回 DAR=3") {
        CHECK(registry.invoke({0x4001, 2, 0}, model::Data{model::UInt8{1}}).dar == 3);
    }

    SECTION("参数与返回类型不符返回 DAR=7") {
        CHECK(registry.invoke({0x4001, 1, 0}, model::Data{model::UInt16{1}}).dar == 7);
        provider->action = ActionValue{0, model::Data{model::UInt8{1}}};
        auto mismatch = registry.invoke({0x4001, 1, 0}, model::Data{model::UInt8{1}});
        CHECK(mismatch.dar == 7);
        CHECK_FALSE(mismatch.data.has_value());
    }

    SECTION("合法方法透传 provider 的 DAR 与可选返回值") {
        provider->action = ActionValue{0, model::Data{model::UInt16{9}}};
        auto ok = registry.invoke({0x4001, 1, 0}, model::Data{model::UInt8{1}});
        CHECK(ok.dar == 0);
        REQUIRE(ok.data.has_value());
        CHECK(*ok.data == model::Data{model::UInt16{9}});
        // 未声明参数与返回类型时，provider 的原始 Data 不被类型检查拒绝。
        provider->action = ActionValue{0, model::Data{model::UInt8{3}}};
        auto any = registry.invoke({0x4001, 3, 0}, model::Data{model::Null{}});
        CHECK(any.dar == 0);
        REQUIRE(any.data.has_value());
        CHECK(*any.data == model::Data{model::UInt8{3}});
    }

    SECTION("provider 抛异常被隔离为 DAR=255") {
        provider->explode = true;
        CHECK(registry.invoke({0x4001, 1, 0}, model::Data{model::UInt8{1}}).dar == 255);
    }

    SECTION("记录分发按可读、记录声明与表头一致性检查") {
        const apdu::GetRecord query{{0x4001, 2, 0}, model::SelectAll{}, {}};
        // 未绑定记录时 provider 的缺省行为是拒绝。
        CHECK(record_dar_of(registry.read_record(query)) == 3);
        // 响应属性与查询不一致时被目录拒绝为 DAR=7。
        provider->record = RecordResult{{0x9999, 2, 0}, {}, std::uint8_t{3}};
        CHECK(record_dar_of(registry.read_record(query)) == 7);
        // 有行数据却没有表头同样是 DAR=7。
        provider->record =
            RecordResult{{0x4001, 2, 0}, {}, std::vector<RecordRow>{{model::UInt16{1}}}};
        CHECK(record_dar_of(registry.read_record(query)) == 7);
        // 行宽与表头不一致同样是 DAR=7。
        provider->record =
            RecordResult{{0x4001, 2, 0},
                         {{model::Oad{0x5004, 2, 0}}},
                         std::vector<RecordRow>{{model::UInt16{1}, model::UInt16{2}}}};
        CHECK(record_dar_of(registry.read_record(query)) == 7);
        // 合法快照原样返回；空列查询允许 provider 给出自己的表头。
        provider->record = RecordResult{{0x4001, 2, 0},
                                        {{model::Oad{0x5004, 2, 0}}},
                                        std::vector<RecordRow>{{model::UInt16{1}}}};
        auto valid = registry.read_record(query);
        REQUIRE(std::holds_alternative<std::vector<RecordRow>>(valid.result));
        CHECK(std::get<std::vector<RecordRow>>(valid.result).size() == 1);
        CHECK(valid.columns == model::Rcsd({model::Oad{0x5004, 2, 0}}));
    }

    SECTION("非记录属性与不可读属性分别返回 DAR=5 和 3") {
        CHECK(record_dar_of(registry.read_record({{0x4001, 3, 0}, model::SelectAll{}, {}})) == 5);
        REQUIRE(static_cast<bool>(registry.register_object(
            ObjectSchema{0x4002, "w", {{2, model::DataType::array, false, false, true}}, {}},
            provider)));
        CHECK(record_dar_of(registry.read_record({{0x4002, 2, 0}, model::SelectAll{}, {}})) == 3);
        CHECK(record_dar_of(registry.read_record({{0x9999, 2, 0}, model::SelectAll{}, {}})) == 4);
    }

    SECTION("指定列查询要求响应表头逐项一致") {
        const apdu::GetRecord query{{0x4001, 2, 0}, model::SelectAll{}, {model::Oad{0x2023, 2, 0}}};
        // 响应表头缺失时与指定列查询不符。
        provider->record =
            RecordResult{{0x4001, 2, 0}, {}, std::vector<RecordRow>{{model::UInt32{1}}}};
        CHECK(record_dar_of(registry.read_record(query)) == 7);
        provider->record = RecordResult{
            {0x4001, 2, 0}, {model::Oad{0x2023, 2, 0}}, std::vector<RecordRow>{{model::UInt32{1}}}};
        const auto rows = registry.read_record(query);
        REQUIRE(std::holds_alternative<std::vector<RecordRow>>(rows.result));
        CHECK(rows.columns == query.columns);
    }

    SECTION("记录 provider 抛异常被隔离为 DAR=255") {
        provider->explode = true;
        CHECK(record_dar_of(registry.read_record({{0x4001, 2, 0}, model::SelectAll{}, {}})) == 255);
    }
}

TEST_CASE("IObjectProvider 缺省实现保持只读拒绝", "[service][object]") {
    ReadOnlyProvider provider;

    SECTION("write 缺省返回 DAR=3") {
        CHECK(provider.write({0x2000, 2, 0}, model::Data{model::UInt16{1}}) == 3);
    }

    SECTION("invoke 缺省返回 DAR=3 且没有返回数据") {
        auto result = provider.invoke({0x2000, 1, 0}, model::Null{});
        CHECK(result.dar == 3);
        CHECK_FALSE(result.data.has_value());
    }

    SECTION("read_record 缺省以 DAR=3 拒绝并回显查询描述符") {
        const apdu::GetRecord query{{0x2000, 2, 0}, model::SelectAll{}, {model::Oad{0x2023, 2, 0}}};
        auto result = provider.read_record(query);
        CHECK(record_dar_of(result) == 3);
        CHECK(result.attribute == query.attribute);
        CHECK(result.columns == query.columns);
    }

    SECTION("经目录调用时缺省拒绝同样生效") {
        ObjectRegistry registry;
        REQUIRE(static_cast<bool>(registry.register_object(
            ObjectSchema{0x2000, "x", {{2, model::DataType::uint16, true, true, false}}, {}},
            std::make_shared<ReadOnlyProvider>())));
        CHECK(registry.write({0x2000, 2, 0}, model::Data{model::UInt16{1}}) == 3);
    }
}

TEST_CASE("MemoryObject 的 set 是本地配置而非协议写入", "[service][object]") {
    auto memory = std::make_shared<MemoryObject>();

    SECTION("set 建立本地属性并可被 read 观察") {
        memory->set(2, model::Data{model::UInt16{220}});
        REQUIRE(std::holds_alternative<model::Data>(memory->read({0x2000, 2, 0})));
        CHECK(std::get<model::Data>(memory->read({0x2000, 2, 0})) ==
              model::Data{model::UInt16{220}});
        // 本地配置不需要目录授权，与协议 SET 服务完全无关。
        CHECK(memory->write({0x2000, 2, 0}, model::Data{model::UInt16{221}}) == 0);
    }

    SECTION("属性编号零和 32 都被拒绝") {
        CHECK_THROWS_AS(memory->set(0, model::Null{}), std::invalid_argument);
        CHECK_THROWS_AS(memory->set(32, model::Null{}), std::invalid_argument);
        CHECK_NOTHROW(memory->set(31, model::Null{}));
    }

    SECTION("未配置的属性返回 DAR=4") {
        CHECK(dar_of(memory->read({0x2000, 9, 0})) == 4);
        CHECK(memory->write({0x2000, 9, 0}, model::Data{model::Null{}}) == 4);
    }

    SECTION("write 不创建未知属性") {
        CHECK(memory->write({0x2000, 5, 0}, model::Data{model::UInt8{1}}) == 4);
        CHECK(dar_of(memory->read({0x2000, 5, 0})) == 4);
    }

    SECTION("整体类型不一致返回 DAR=7") {
        memory->set(2, model::Data{model::UInt16{1}});
        CHECK(memory->write({0x2000, 2, 0}, model::Data{model::UInt32{1}}) == 7);
        CHECK(memory->write({0x2000, 2, 0}, model::Data{model::UInt16{2}}) == 0);
    }
}

TEST_CASE("MemoryObject 的一级元素索引读写", "[service][object]") {
    auto memory = std::make_shared<MemoryObject>();
    memory->set(3, model::Array{{model::UInt16{10}, model::UInt16{20}, model::UInt16{30}}});

    SECTION("索引从 1 开始，零表示整体") {
        auto second = memory->read({0x2000, 3, 2});
        REQUIRE(std::holds_alternative<model::Data>(second));
        CHECK(std::get<model::Data>(second) == model::Data{model::UInt16{20}});
        REQUIRE(std::holds_alternative<model::Data>(memory->read({0x2000, 3, 0})));
        CHECK(std::get<model::Data>(memory->read({0x2000, 3, 0})).type() == model::DataType::array);
    }

    SECTION("越界索引返回 DAR=8，写入同样受限") {
        CHECK(dar_of(memory->read({0x2000, 3, 4})) == 8);
        CHECK(memory->write({0x2000, 3, 4}, model::Data{model::UInt16{40}}) == 8);
    }

    SECTION("非数组与结构不支持元素索引") {
        memory->set(4, model::Data{model::UInt16{1}});
        CHECK(dar_of(memory->read({0x2000, 4, 1})) == 8);
        CHECK(memory->write({0x2000, 4, 1}, model::Data{model::UInt16{1}}) == 8);
    }

    SECTION("元素类型不一致返回 DAR=7 且不改变原值") {
        CHECK(memory->write({0x2000, 3, 1}, model::Data{model::UInt8{99}}) == 7);
        auto first = memory->read({0x2000, 3, 1});
        REQUIRE(std::holds_alternative<model::Data>(first));
        CHECK(std::get<model::Data>(first) == model::Data{model::UInt16{10}});
    }

    SECTION("整体替换保持容器标签") {
        CHECK(memory->write({0x2000, 3, 0}, model::Array{{model::UInt16{1}}}) == 0);
        auto value = memory->read({0x2000, 3, 0});
        REQUIRE(std::holds_alternative<model::Data>(value));
        CHECK(std::get<model::Data>(value).type() == model::DataType::array);
        // 元素数量随整体替换变化，越界索引随之改变。
        CHECK(dar_of(memory->read({0x2000, 3, 2})) == 8);
    }

    SECTION("结构的一级字段同样可读写且不与数组混淆") {
        memory->set(5, model::Structure{{model::UInt8{1}, model::UInt8{2}}});
        auto field = memory->read({0x2000, 5, 2});
        REQUIRE(std::holds_alternative<model::Data>(field));
        CHECK(std::get<model::Data>(field) == model::Data{model::UInt8{2}});
        CHECK(memory->write({0x2000, 5, 2}, model::Data{model::UInt8{7}}) == 0);
        REQUIRE(std::holds_alternative<model::Data>(memory->read({0x2000, 5, 2})));
        CHECK(std::get<model::Data>(memory->read({0x2000, 5, 2})) == model::Data{model::UInt8{7}});
        // 结构整体替换必须保持 structure 标签。
        CHECK(memory->write({0x2000, 5, 0}, model::Structure{{model::UInt8{5}}}) == 0);
        REQUIRE(std::holds_alternative<model::Data>(memory->read({0x2000, 5, 0})));
        CHECK(std::get<model::Data>(memory->read({0x2000, 5, 0})).type() ==
              model::DataType::structure);
        CHECK(dar_of(memory->read({0x2000, 5, 2})) == 8);
    }

    SECTION("已交付的旧 Data 不受后续写入影响") {
        auto snapshot = memory->read({0x2000, 3, 0});
        REQUIRE(std::holds_alternative<model::Data>(snapshot));
        REQUIRE(memory->write({0x2000, 3, 1}, model::Data{model::UInt16{99}}) == 0);
        CHECK(std::get<model::Data>(snapshot) ==
              model::Array{{model::UInt16{10}, model::UInt16{20}, model::UInt16{30}}});
    }
}

TEST_CASE("MemoryObject 的方法与记录绑定", "[service][object]") {
    auto memory = std::make_shared<MemoryObject>();

    SECTION("方法编号零与空回调被拒绝，255 合法") {
        CHECK_THROWS_AS(memory->bind_method(
                            0, [](const model::Omd&, const model::Data&) { return ActionValue{}; }),
                        std::invalid_argument);
        CHECK_THROWS_AS(memory->bind_method(1, nullptr), std::invalid_argument);
        CHECK_NOTHROW(memory->bind_method(
            255, [](const model::Omd&, const model::Data&) { return ActionValue{}; }));
    }

    SECTION("记录属性编号越界与空回调被拒绝") {
        CHECK_THROWS_AS(
            memory->bind_record(0, [](const apdu::GetRecord&) { return RecordResult{}; }),
            std::invalid_argument);
        CHECK_THROWS_AS(
            memory->bind_record(32, [](const apdu::GetRecord&) { return RecordResult{}; }),
            std::invalid_argument);
        CHECK_THROWS_AS(memory->bind_record(2, nullptr), std::invalid_argument);
        CHECK_NOTHROW(
            memory->bind_record(31, [](const apdu::GetRecord&) { return RecordResult{}; }));
    }

    SECTION("未绑定的方法与记录返回 DAR=4") {
        CHECK(memory->invoke({0x2000, 1, 0}, model::Null{}).dar == 4);
        auto unbound = memory->read_record({{0x2000, 2, 0}, model::SelectAll{}, {}});
        CHECK(record_dar_of(unbound) == 4);
    }

    SECTION("非零模式的方法返回 DAR=3") {
        memory->bind_method(1, [](const model::Omd&, const model::Data&) {
            return ActionValue{0, model::Data{model::UInt8{1}}};
        });
        auto rejected = memory->invoke({0x2000, 1, 1}, model::Null{});
        CHECK(rejected.dar == 3);
        CHECK_FALSE(rejected.data.has_value());
        auto accepted = memory->invoke({0x2000, 1, 0}, model::Null{});
        CHECK(accepted.dar == 0);
        REQUIRE(accepted.data.has_value());
        CHECK(*accepted.data == model::Data{model::UInt8{1}});
    }

    SECTION("方法和记录回调可重入本对象的读写") {
        memory->set(2, model::Data{model::UInt16{5}});
        memory->bind_method(1, [memory](const model::Omd&, const model::Data&) {
            // 回调内读取并改写属性，验证对象锁已释放。
            auto value = memory->read({0x2000, 2, 0});
            const auto raw = std::get<model::Data>(value).as<model::UInt16>().value;
            memory->write({0x2000, 2, 0},
                          model::Data{model::UInt16{static_cast<std::uint16_t>(raw + 1)}});
            return ActionValue{0, model::Data{model::UInt8{static_cast<std::uint8_t>(raw)}}};
        });
        auto result = memory->invoke({0x2000, 1, 0}, model::Data{model::Null{}});
        REQUIRE(result.data.has_value());
        CHECK(*result.data == model::Data{model::UInt8{5}});
        REQUIRE(std::holds_alternative<model::Data>(memory->read({0x2000, 2, 0})));
        CHECK(std::get<model::Data>(memory->read({0x2000, 2, 0})) == model::Data{model::UInt16{6}});

        memory->set(3, model::Array{{model::UInt16{1}, model::UInt16{2}}});
        memory->bind_record(2, [memory](const apdu::GetRecord& query) {
            auto value = memory->read({0x2000, 3, 0});
            const std::uint8_t count = std::get<model::Data>(value).as<model::Array>().value.size();
            return RecordResult{query.attribute, query.columns,
                                std::vector<RecordRow>{{model::UInt8{count}}}};
        });
        auto record = memory->read_record({{0x2000, 2, 0}, model::SelectAll{}, {}});
        REQUIRE(std::holds_alternative<std::vector<RecordRow>>(record.result));
        CHECK(std::get<std::vector<RecordRow>>(record.result).at(0).at(0) ==
              model::Data{model::UInt8{2}});
    }

    SECTION("重复绑定同一编号以后者为准") {
        memory->bind_method(1, [](const model::Omd&, const model::Data&) { return ActionValue{}; });
        memory->bind_method(1, [](const model::Omd&, const model::Data&) {
            return ActionValue{0, model::Data{model::UInt8{42}}};
        });
        auto result = memory->invoke({0x2000, 1, 0}, model::Null{});
        REQUIRE(result.data.has_value());
        CHECK(*result.data == model::Data{model::UInt8{42}});
    }

    SECTION("记录处理器抛出的异常由目录转换为 DAR=255") {
        ObjectRegistry registry;
        memory->bind_record(
            2, [](const apdu::GetRecord&) -> RecordResult { throw std::runtime_error("boom"); });
        REQUIRE(static_cast<bool>(registry.register_object(
            ObjectSchema{0x5004, "x", {{2, model::DataType::array, true, false, true}}, {}},
            memory)));
        CHECK(record_dar_of(registry.read_record({{0x5004, 2, 0}, model::SelectAll{}, {}})) == 255);
    }
}

/**
 * @file service_test.cpp
 * @brief 客户机/服务器异步服务在真实会话对上的分发、单在途事务与错误回调测试。
 */
#include <dlt698/service/service.hpp>
#include <dlt698/service/standard_object.hpp>
#include <dlt698/transport/memory.hpp>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::service;
namespace oi = standard::oi;
namespace apdu = protocol::apdu;
using apdu::RecordResult;
using apdu::RecordRow;

namespace {

/// 断言读取结果是 DAR 并返回原码。
std::uint8_t dar_of(const ObjectValue& value) {
    REQUIRE(std::holds_alternative<std::uint8_t>(value));
    return std::get<std::uint8_t>(value);
}

/// 断言记录结果是 DAR 并返回原码。
std::uint8_t record_dar_of(const RecordResult& result) {
    REQUIRE(std::holds_alternative<std::uint8_t>(result.result));
    return std::get<std::uint8_t>(result.result);
}

/// 断言结果是 Data 并与期望值比较。
void check_data(const ObjectValue& value, const model::Data& expected) {
    REQUIRE(std::holds_alternative<model::Data>(value));
    CHECK(std::get<model::Data>(value) == expected);
}

/**
 * @brief 一对通过内存通道连接的预设关联会话。
 * @note 预设关联跳过 CONNECT，使读写用例只聚焦服务层分发；状态机行为由 session_test 覆盖。
 */
struct Peers {
    std::shared_ptr<ManualExecutor> executor = std::make_shared<ManualExecutor>();
    std::shared_ptr<transport::MemoryChannel> first, second;
    std::shared_ptr<session::Session> client, server;
    std::shared_ptr<ObjectRegistry> registry = std::make_shared<ObjectRegistry>();
    std::unique_ptr<ServerService> service;

    /// 构造会话对；capability 用于逐项关闭一致性位以验证未协商服务的拒绝。
    Peers(bool preset = true, std::function<void(session::SessionOptions&)> tweak = {}) {
        auto channels = transport::MemoryChannel::pair(executor);
        first = channels.first;
        second = channels.second;
        session::SessionOptions client_options, server_options;
        server_options.role = session::Role::server;
        client_options.preset_association = server_options.preset_association = preset;
        if (tweak) {
            tweak(client_options);
            tweak(server_options);
        }
        client = std::make_shared<session::Session>(first, executor, client_options);
        server = std::make_shared<session::Session>(second, executor, server_options);
        client->start();
        server->start();
        executor->run_ready();
    }

    ~Peers() {
        client->close();
        server->close();
        executor->run_ready();
    }

    /// 装配服务器服务；目录必须先完成注册。
    void serve() {
        service = std::make_unique<ServerService>(server, registry);
        executor->run_ready();
    }
};

/// 计数并返回固定值的 provider，用于观察服务器实际收到的请求数。
struct CountingProvider final : IObjectProvider {
    model::Data value{model::Data{model::UInt16{220}}};
    unsigned reads = 0;

    ObjectValue read(const model::Oad&) override {
        ++reads;
        return value;
    }
};

/// 三相四费率电能数组，元素数量与默认设备配置一致。
model::Data energy() {
    return model::Data{model::Array{{model::UInt32{100}, model::UInt32{10}, model::UInt32{20},
                                     model::UInt32{30}, model::UInt32{40}}}};
}

/// 冻结记录的一列固定表头。
model::Rcsd freeze_columns() { return {model::Oad{oi::freeze_time, 2, 0}}; }

/// 日冻结的一行记录：DateTimeS 保留年/月/日/时/分/秒七个原始字段。
RecordRow freeze_row(std::uint8_t day) {
    return {model::Data{model::DateTimeS{{0x07, static_cast<std::uint8_t>(0xe0 + day / 28),
                                          static_cast<std::uint8_t>(1 + day / 28),
                                          static_cast<std::uint8_t>(1 + day % 28), 0, 0, 0}}}};
}

}  // namespace

TEST_CASE("客户机与服务器服务在会话对上完成读写分发", "[service][client][server]") {
    Peers peers;
    auto energy_provider = std::make_shared<CountingProvider>();
    energy_provider->value = energy();
    REQUIRE(static_cast<bool>(register_standard_object(*peers.registry, oi::forward_active_energy,
                                                       {2}, energy_provider)));
    // 电能对象是只读的；写入用例另建一个显式可写的厂家对象。
    auto writable = std::make_shared<MemoryObject>();
    writable->set(2, model::Data{model::UInt16{1}});
    REQUIRE(static_cast<bool>(peers.registry->register_object(
        ObjectSchema{0x2000, "厂家可写", {{2, model::DataType::uint16, true, true, false}}, {}},
        writable)));
    // 带方法的手写 schema，用于 ACTION 分发。
    auto methods = std::make_shared<MemoryObject>();
    methods->set(2, model::Data{model::UInt8{7}});
    methods->bind_method(1, [methods](const model::Omd& method, const model::Data& parameter) {
        // 回调内重入对象读取，验证目录锁外调用。
        auto current = methods->read({method.oi, 2, 0});
        const auto held = std::get<model::Data>(current).as<model::UInt8>().value;
        return ActionValue{0, model::Data{model::UInt16{static_cast<std::uint16_t>(
                                  held + parameter.as<model::UInt8>().value)}}};
    });
    REQUIRE(static_cast<bool>(peers.registry->register_object(
        ObjectSchema{0x4000,
                     "厂家方法",
                     {{2, model::DataType::uint8, true, false, false}},
                     {{1, model::DataType::uint8, model::DataType::uint16, true}}},
        methods)));
    peers.serve();

    ClientService client(peers.client);

    SECTION("async_get 读取单个属性并保留精确类型") {
        std::optional<Result<ObjectValue>> response;
        unsigned callbacks = 0;
        client.async_get({oi::forward_active_energy, 2, 0}, [&](auto result) {
            ++callbacks;
            response = std::move(result);
        });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        check_data(response->value(), energy());
        CHECK(callbacks == 1);
        CHECK(energy_provider->reads == 1);
    }

    SECTION("async_get 的逐项 DAR 原样交给调用方，外层 Result 仍然成功") {
        std::optional<Result<ObjectValue>> response;
        // 未注册的对象是 DAR=4，属于业务结果而不是事务失败。
        client.async_get({0xf001, 2, 0}, [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        CHECK(dar_of(response->value()) == 4);
    }

    SECTION("async_get_list 保留逐项结果与原顺序") {
        const std::vector<model::Oad> points{
            {oi::forward_active_energy, 2, 0}, {0xf001, 2, 0}, {0x2000, 2, 0}};
        std::optional<Result<apdu::GetResponse>> response;
        client.async_get_list(points, [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        REQUIRE(response->value().attributes.size() == 3);
        CHECK(response->value().list);
        for (std::size_t i = 0; i < points.size(); ++i)
            CHECK(response->value().attributes[i].attribute == points[i]);
        check_data(response->value().attributes[0].result, energy());
        CHECK(dar_of(response->value().attributes[1].result) == 4);
        check_data(response->value().attributes[2].result, model::Data{model::UInt16{1}});
    }

    SECTION("async_set 提交写入并返回原始 DAR") {
        std::optional<Result<std::uint8_t>> response;
        // 厂家对象显式可写，值类型一致时成功。
        client.async_set({0x2000, 2, 0}, model::Data{model::UInt16{5}},
                         [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        CHECK(response->value() == 0);
        auto stored = writable->read({0x2000, 2, 0});
        REQUIRE(std::holds_alternative<model::Data>(stored));
        CHECK(std::get<model::Data>(stored) == model::Data{model::UInt16{5}});
    }

    SECTION("标准只读对象的写入返回 DAR=3，不自动重试也不回滚") {
        std::optional<Result<std::uint8_t>> response;
        client.async_set({oi::forward_active_energy, 2, 0}, energy(),
                         [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        CHECK(response->value() == 3);
        // 远端拒绝不改变已有值。
        check_data(energy_provider->read({oi::forward_active_energy, 2, 0}), energy());
    }

    SECTION("async_set_list 逐项返回 DAR，部分成功不回滚") {
        std::optional<Result<apdu::SetResponse>> response;
        client.async_set_list(
            std::vector<apdu::SetAttribute>{
                {model::Oad{0x2000, 2, 0}, model::Data{model::UInt16{9}}},
                {model::Oad{oi::forward_active_energy, 2, 0}, energy()}},
            [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        REQUIRE(response->value().attributes.size() == 2);
        CHECK(response->value().list);
        CHECK(response->value().attributes[0].attribute == (model::Oad{0x2000, 2, 0}));
        CHECK(response->value().attributes[0].dar == 0);
        CHECK(response->value().attributes[1].dar == 3);
        auto stored = writable->read({0x2000, 2, 0});
        REQUIRE(std::holds_alternative<model::Data>(stored));
        CHECK(std::get<model::Data>(stored) == model::Data{model::UInt16{9}});
    }

    SECTION("async_action 返回方法结果与可选返回数据") {
        std::optional<Result<ActionValue>> response;
        client.async_action({0x4000, 1, 0}, model::Data{model::UInt8{3}},
                            [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        CHECK(response->value().dar == 0);
        REQUIRE(response->value().data.has_value());
        // 方法回调读到属性 7 并加上参数 3。
        CHECK(*response->value().data == model::Data{model::UInt16{10}});
    }

    SECTION("async_action 的逐项 DAR 保留，未注册方法是 DAR=4") {
        std::optional<Result<ActionValue>> response;
        client.async_action({0xf001, 1, 0}, model::Data{model::UInt8{1}},
                            [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        CHECK(response->value().dar == 4);
        CHECK_FALSE(response->value().data.has_value());
    }

    SECTION("async_action_list 顺序执行多个方法并保留每项结果") {
        std::optional<Result<apdu::ActionResponse>> response;
        client.async_action_list({{model::Omd{0x4000, 1, 0}, model::Data{model::UInt8{1}}},
                                  {model::Omd{0xf001, 1, 0}, model::Data{model::UInt8{1}}},
                                  {model::Omd{0x4000, 1, 0}, model::Data{model::UInt8{2}}}},
                                 [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        REQUIRE(response->value().methods.size() == 3);
        CHECK(response->value().list);
        CHECK(response->value().methods[0].method == (model::Omd{0x4000, 1, 0}));
        CHECK(response->value().methods[0].dar == 0);
        CHECK(*response->value().methods[0].data == model::Data{model::UInt16{8}});
        CHECK(response->value().methods[1].dar == 4);
        // 顺序执行意味着第三项看到第二项之前的对象状态。
        CHECK(*response->value().methods[2].data == model::Data{model::UInt16{9}});
    }
}

TEST_CASE("服务器服务分发记录查询", "[service][client][server][record]") {
    Peers peers;
    auto records = std::make_shared<MemoryObject>();
    const auto columns = freeze_columns();
    records->bind_record(2, [columns](const apdu::GetRecord& query) {
        // 表头按查询的列选择原样返回，行宽与表头保持一致。
        const model::Rcsd header = query.columns.empty() ? columns : query.columns;
        std::vector<RecordRow> rows;
        if (query.columns.size() == 1)
            for (std::uint8_t day = 1; day <= 2; ++day) rows.push_back(freeze_row(day));
        return RecordResult{query.attribute, header, rows};
    });
    REQUIRE(static_cast<bool>(
        register_standard_object(*peers.registry, oi::daily_freeze, {2}, records)));
    // 月冻结未绑定记录处理器，缺省 DAR=4。
    auto monthly = std::make_shared<MemoryObject>();
    REQUIRE(static_cast<bool>(
        register_standard_object(*peers.registry, oi::monthly_freeze, {2}, monthly)));
    peers.serve();

    ClientService client(peers.client);

    SECTION("async_get_record 返回单条记录的完整快照") {
        std::optional<Result<RecordResult>> response;
        client.async_get_record({{oi::daily_freeze, 2, 0}, model::SelectAll{}, columns},
                                [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        REQUIRE(std::holds_alternative<std::vector<RecordRow>>(response->value().result));
        CHECK(response->value().columns == columns);
        CHECK(std::get<std::vector<RecordRow>>(response->value().result).size() == 2);
    }

    SECTION("async_get_record 的 DAR 保留在 Result 内") {
        std::optional<Result<RecordResult>> response;
        client.async_get_record({{oi::monthly_freeze, 2, 0}, model::SelectAll{}, {}},
                                [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        CHECK(record_dar_of(response->value()) == 4);
    }

    SECTION("async_get_record_list 逐项保留 DAR 与顺序") {
        std::optional<Result<apdu::GetRecordResponse>> response;
        const apdu::GetRecord daily{{oi::daily_freeze, 2, 0}, model::SelectAll{}, columns};
        const apdu::GetRecord monthly{{oi::monthly_freeze, 2, 0}, model::SelectAll{}, {}};
        client.async_get_record_list({daily, monthly},
                                     [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        REQUIRE(response->value().records.size() == 2);
        CHECK(response->value().list);
        CHECK(response->value().records[0].attribute == (model::Oad{oi::daily_freeze, 2, 0}));
        REQUIRE(
            std::holds_alternative<std::vector<RecordRow>>(response->value().records[0].result));
        CHECK(response->value().records[1].attribute == (model::Oad{oi::monthly_freeze, 2, 0}));
        CHECK(record_dar_of(response->value().records[1]) == 4);
    }

    SECTION("记录型对象不能按普通属性读取") {
        std::optional<Result<ObjectValue>> response;
        client.async_get({oi::daily_freeze, 2, 0},
                         [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        CHECK(dar_of(response->value()) == 3);
    }
}

TEST_CASE("服务层只允许一个在途事务", "[service][client]") {
    Peers peers;
    auto provider = std::make_shared<CountingProvider>();
    REQUIRE(static_cast<bool>(peers.registry->register_object(
        ObjectSchema{0x2000, "厂家", {{2, model::DataType::uint16}}, {}}, provider)));
    peers.serve();
    ClientService client(peers.client);

    SECTION("第二个请求立即返回 busy 且不入队列") {
        std::optional<Result<ObjectValue>> first, second;
        unsigned second_callbacks = 0;
        // 不在两次提交之间驱动执行器，第一个请求已经占据在途事务。
        client.async_get({0x2000, 2, 0}, [&](auto result) { first = std::move(result); });
        client.async_get({0x2000, 2, 0}, [&](auto result) {
            ++second_callbacks;
            second = std::move(result);
        });
        peers.executor->run_ready();
        // 第一个请求正常完成。
        REQUIRE(first);
        REQUIRE(static_cast<bool>(*first));
        check_data(first->value(), provider->value);
        // 第二个请求被立即拒绝，不排队等待。
        REQUIRE(second);
        test::require_error(*second, ErrorCode::busy);
        CHECK(second->error().context == "session transaction");
        CHECK(second_callbacks == 1);
        // 服务器只收到一次请求，被拒绝的请求没有上线。
        CHECK(provider->reads == 1);
        // 拒绝之后仍可继续发起新事务。
        std::optional<Result<ObjectValue>> third;
        client.async_get({0x2000, 2, 0}, [&](auto result) { third = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(third);
        REQUIRE(static_cast<bool>(*third));
        CHECK(provider->reads == 2);
    }

    SECTION("读事务在途时写入同样返回 busy") {
        std::optional<Result<ObjectValue>> read_result;
        std::optional<Result<std::uint8_t>> set_result;
        client.async_get({0x2000, 2, 0}, [&](auto result) { read_result = std::move(result); });
        client.async_set({0x2000, 2, 0}, model::Data{model::UInt16{1}},
                         [&](auto result) { set_result = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(read_result);
        REQUIRE(static_cast<bool>(*read_result));
        REQUIRE(set_result);
        test::require_error(*set_result, ErrorCode::busy);
    }

    SECTION("回调在执行器内串行执行，可在回调中投递后续请求") {
        std::optional<Result<ObjectValue>> outer, inner;
        unsigned depth = 0, max_depth = 0;
        client.async_get({0x2000, 2, 0}, [&](auto result) {
            ++depth;
            max_depth = std::max(max_depth, depth);
            // 回调运行在执行器中，此时提交新请求是排队而不是重入。
            client.async_get({0x2000, 2, 0}, [&](auto next) {
                ++depth;
                max_depth = std::max(max_depth, depth);
                inner = std::move(next);
                --depth;
            });
            outer = std::move(result);
            --depth;
        });
        peers.executor->run_ready();
        REQUIRE(outer);
        REQUIRE(inner);
        REQUIRE(static_cast<bool>(*outer));
        REQUIRE(static_cast<bool>(*inner));
        // 回调不会互相嵌套，重入深度始终为一。
        CHECK(max_depth == 1);
        CHECK(provider->reads == 2);
    }
}

TEST_CASE("服务层的本地错误在事务发起前被拒绝", "[service][client]") {
    SECTION("未关联的会话返回 not_associated 而不是排队") {
        Peers peers(/*preset=*/false);
        auto provider = std::make_shared<CountingProvider>();
        REQUIRE(static_cast<bool>(peers.registry->register_object(
            ObjectSchema{0x2000, "厂家", {{2, model::DataType::uint16}}, {}}, provider)));
        peers.serve();
        ClientService client(peers.client);

        std::optional<Result<ObjectValue>> response;
        client.async_get({0x2000, 2, 0}, [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        test::require_error(*response, ErrorCode::not_associated);
        // 事务没有上线，服务器没有收到任何请求。
        CHECK(provider->reads == 0);
    }

    SECTION("未协商的读写能力返回 unsupported_service") {
        // 能力序号 8（SET NormalList）对应 protocol[1] 的最高位，关闭后列表写入不再可用。
        Peers peers(
            true, [](session::SessionOptions& options) { options.parameters.protocol[1] &= 0x7f; });
        auto writable = std::make_shared<MemoryObject>();
        writable->set(2, model::Data{model::UInt16{1}});
        REQUIRE(static_cast<bool>(peers.registry->register_object(
            ObjectSchema{0x2000, "厂家", {{2, model::DataType::uint16, true, true, false}}, {}},
            writable)));
        peers.serve();
        ClientService client(peers.client);

        std::optional<Result<apdu::SetResponse>> response;
        client.async_set_list(std::vector<apdu::SetAttribute>{{model::Oad{0x2000, 2, 0},
                                                               model::Data{model::UInt16{2}}}},
                              [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        test::require_error(*response, ErrorCode::unsupported_service);
        CHECK(response->error().context == "mutation not negotiated");
        // 普通 SET 仍然可用，说明只关闭了列表能力。
        std::optional<Result<std::uint8_t>> single;
        client.async_set({0x2000, 2, 0}, model::Data{model::UInt16{3}},
                         [&](auto result) { single = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(single);
        REQUIRE(static_cast<bool>(*single));
        CHECK(single->value() == 0);
    }

    SECTION("会话关闭后所有方法返回 closed") {
        Peers peers;
        auto provider = std::make_shared<CountingProvider>();
        REQUIRE(static_cast<bool>(peers.registry->register_object(
            ObjectSchema{0x2000, "厂家", {{2, model::DataType::uint16}}, {}}, provider)));
        peers.serve();
        ClientService client(peers.client);
        peers.client->close();
        peers.executor->run_ready();

        std::optional<Result<ObjectValue>> get_result;
        std::optional<Result<std::uint8_t>> set_result;
        std::optional<Result<ActionValue>> action_result;
        std::optional<Result<RecordResult>> record_result;
        client.async_get({0x2000, 2, 0}, [&](auto result) { get_result = std::move(result); });
        client.async_set({0x2000, 2, 0}, model::Data{model::UInt16{1}},
                         [&](auto result) { set_result = std::move(result); });
        client.async_action({0x4000, 1, 0}, model::Data{model::UInt8{1}},
                            [&](auto result) { action_result = std::move(result); });
        client.async_get_record({{oi::daily_freeze, 2, 0}, model::SelectAll{}, {}},
                                [&](auto result) { record_result = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(get_result);
        REQUIRE(set_result);
        REQUIRE(action_result);
        REQUIRE(record_result);
        test::require_error(*get_result, ErrorCode::closed);
        test::require_error(*set_result, ErrorCode::closed);
        test::require_error(*action_result, ErrorCode::closed);
        test::require_error(*record_result, ErrorCode::closed);
        CHECK(provider->reads == 0);
    }

    SECTION("服务器拒绝时事务以 remote_error 结束") {
        Peers peers;
        // 不注册任何处理器时，服务器对每个属性独立返回 DAR=4。
        peers.serve();
        ClientService client(peers.client);
        std::optional<Result<ObjectValue>> response;
        client.async_get({0x2000, 2, 0}, [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        CHECK(dar_of(response->value()) == 4);
    }
}

TEST_CASE("服务器服务的构造参数校验", "[service][server]") {
    Peers peers;

    SECTION("空会话或空目录被拒绝") {
        CHECK_THROWS_AS(ServerService(nullptr, peers.registry), std::invalid_argument);
        CHECK_THROWS_AS(ServerService(peers.server, nullptr), std::invalid_argument);
        CHECK_NOTHROW(ServerService(peers.server, peers.registry));
    }

    SECTION("客户机服务拒绝空会话") {
        CHECK_THROWS_AS(ClientService(nullptr), std::invalid_argument);
        CHECK_NOTHROW(ClientService(peers.client));
    }
}

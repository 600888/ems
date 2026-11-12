/**
 * @file sync_test.cpp
 * @brief 同步客户机封装的返回值、远端拒绝、释放完成与驱动异常测试。
 */
#include <dlt698/service/standard_object.hpp>
#include <dlt698/service/sync.hpp>
#include <dlt698/transport/memory.hpp>
#include <stdexcept>

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

/// 断言结果是 Data 并与期望值比较。
void check_data(const ObjectValue& value, const model::Data& expected) {
    REQUIRE(std::holds_alternative<model::Data>(value));
    CHECK(std::get<model::Data>(value) == expected);
}

/**
 * @brief 预设关联的会话对，附带由调用线程驱动的同步执行器。
 * @note drive 在调用线程上推进虚拟时间并执行到期任务，因此同步调用不会自锁。
 */
struct Peers {
    std::shared_ptr<ManualExecutor> executor = std::make_shared<ManualExecutor>();
    std::shared_ptr<session::Session> client, server;
    std::shared_ptr<ObjectRegistry> registry = std::make_shared<ObjectRegistry>();
    std::unique_ptr<ServerService> service;

    /// 构造会话对；preset=false 时走真实 CONNECT，version 不同于 0x0010 时服务器拒绝。
    Peers(bool preset = true, std::uint16_t version = 0x0010) {
        auto channels = transport::MemoryChannel::pair(executor);
        session::SessionOptions client_options, server_options;
        server_options.role = session::Role::server;
        client_options.preset_association = server_options.preset_association = preset;
        server_options.parameters.version = version;
        client = std::make_shared<session::Session>(channels.first, executor, client_options);
        server = std::make_shared<session::Session>(channels.second, executor, server_options);
        client->start();
        server->start();
        executor->run_ready();
    }

    ~Peers() {
        client->close();
        server->close();
        executor->run_ready();
    }

    /// 装配服务器服务。
    void serve() {
        service = std::make_unique<ServerService>(server, registry);
        executor->run_ready();
    }

    /// 供同步封装使用的驱动：推进虚拟时间并执行到期任务。
    void drive(std::chrono::milliseconds elapsed) { executor->advance(elapsed); }
};

/// 厂家对象：属性 2 是可读写数值，属性 3 是记录入口，方法 1 可执行。
std::shared_ptr<MemoryObject> make_vendor_object() {
    auto object = std::make_shared<MemoryObject>();
    object->set(2, model::Data{model::UInt16{1}});
    object->bind_method(1, [object](const model::Omd&, const model::Data& parameter) {
        auto current = object->read({0x4000, 2, 0});
        const auto held = std::get<model::Data>(current).as<model::UInt16>().value;
        return ActionValue{0, model::Data{model::UInt16{static_cast<std::uint16_t>(
                                  held + parameter.as<model::UInt8>().value)}}};
    });
    // 记录处理器按查询的列数生成等宽行，避免与目录的行宽校验冲突。
    object->bind_record(3, [](const apdu::GetRecord& query) {
        std::vector<RecordRow> rows;
        for (std::uint8_t index = 1; index <= 2; ++index) {
            RecordRow row;
            for (std::size_t i = 0; i < query.columns.size(); ++i)
                row.push_back(model::Data{model::UInt32{static_cast<std::uint32_t>(index)}});
            if (!row.empty()) rows.push_back(std::move(row));
        }
        return RecordResult{query.attribute, query.columns, rows};
    });
    return object;
}

/// 注册厂家对象；记录入口与普通数值分属不同属性编号，因为记录型属性不可按普通属性读取。
void register_vendor(ObjectRegistry& registry, const std::shared_ptr<MemoryObject>& provider) {
    REQUIRE(static_cast<bool>(registry.register_object(
        ObjectSchema{0x4000,
                     "厂家对象",
                     {{2, model::DataType::uint16, true, true, false},
                      {3, model::DataType::array, true, false, true}},
                     {{1, model::DataType::uint8, model::DataType::uint16, true}}},
        provider)));
}

}  // namespace

TEST_CASE("同步客户机在各事务上返回与异步层一致的结果", "[service][sync]") {
    Peers peers;
    auto provider = make_vendor_object();
    register_vendor(*peers.registry, provider);
    peers.serve();
    SyncClientService sync(peers.client,
                           [&](std::chrono::milliseconds elapsed) { peers.drive(elapsed); });

    SECTION("get 返回精确 Data") {
        auto value = sync.get({0x4000, 2, 0});
        REQUIRE(static_cast<bool>(value));
        check_data(value.value(), model::Data{model::UInt16{1}});
    }

    SECTION("get 的 DAR 是业务结果而不是事务错误") {
        auto value = sync.get({0xf001, 2, 0});
        REQUIRE(static_cast<bool>(value));
        CHECK(dar_of(value.value()) == 4);
    }

    SECTION("get_list 保留逐项结果与顺序") {
        auto response = sync.get_list({{0x4000, 2, 0}, {0xf001, 2, 0}});
        REQUIRE(static_cast<bool>(response));
        REQUIRE(response.value().attributes.size() == 2);
        CHECK(response.value().list);
        check_data(response.value().attributes[0].result, model::Data{model::UInt16{1}});
        CHECK(dar_of(response.value().attributes[1].result) == 4);
    }

    SECTION("set 提交写入并返回原始 DAR") {
        auto dar = sync.set({0x4000, 2, 0}, model::Data{model::UInt16{4}});
        REQUIRE(static_cast<bool>(dar));
        CHECK(dar.value() == 0);
        auto stored = provider->read({0x4000, 2, 0});
        REQUIRE(std::holds_alternative<model::Data>(stored));
        CHECK(std::get<model::Data>(stored) == model::Data{model::UInt16{4}});
        // 类型不符的属性返回 DAR=7，同步封装不把它当作失败。
        auto mismatch = sync.set({0x4000, 2, 0}, model::Data{model::UInt8{1}});
        REQUIRE(static_cast<bool>(mismatch));
        CHECK(mismatch.value() == 7);
    }

    SECTION("set_list 逐项返回 DAR，部分成功不回滚") {
        auto response = sync.set_list({{model::Oad{0x4000, 2, 0}, model::Data{model::UInt16{6}}},
                                       {model::Oad{0xf001, 2, 0}, model::Data{model::UInt16{6}}}});
        REQUIRE(static_cast<bool>(response));
        REQUIRE(response.value().attributes.size() == 2);
        CHECK(response.value().attributes[0].dar == 0);
        // 未注册对象返回 DAR=4，已写入项保持生效。
        CHECK(response.value().attributes[1].dar == 4);
        auto stored = provider->read({0x4000, 2, 0});
        REQUIRE(std::holds_alternative<model::Data>(stored));
        CHECK(std::get<model::Data>(stored) == model::Data{model::UInt16{6}});
    }

    SECTION("action 返回方法结果与可选返回数据") {
        auto result = sync.action({0x4000, 1, 0}, model::Data{model::UInt8{5}});
        REQUIRE(static_cast<bool>(result));
        CHECK(result.value().dar == 0);
        REQUIRE(result.value().data.has_value());
        CHECK(*result.value().data == model::Data{model::UInt16{6}});
    }

    SECTION("action_list 顺序执行并保留每项结果") {
        auto response =
            sync.action_list({{model::Omd{0x4000, 1, 0}, model::Data{model::UInt8{1}}},
                              {model::Omd{0xf001, 1, 0}, model::Data{model::UInt8{1}}},
                              {model::Omd{0x4000, 1, 0}, model::Data{model::UInt8{2}}}});
        REQUIRE(static_cast<bool>(response));
        REQUIRE(response.value().methods.size() == 3);
        CHECK(response.value().methods[0].dar == 0);
        CHECK(*response.value().methods[0].data == model::Data{model::UInt16{2}});
        CHECK(response.value().methods[1].dar == 4);
        // 顺序执行：第三项在第二项之后运行，方法回调读到的仍是同一个属性值 1。
        CHECK(*response.value().methods[2].data == model::Data{model::UInt16{3}});
    }

    SECTION("get_record 返回完整行列快照") {
        const apdu::GetRecord query{{0x4000, 3, 0},
                                    model::SelectAll{},
                                    {model::Oad{0x2023, 2, 0}, model::Oad{0x2022, 2, 0}}};
        auto result = sync.get_record(query);
        REQUIRE(static_cast<bool>(result));
        REQUIRE(std::holds_alternative<std::vector<RecordRow>>(result.value().result));
        CHECK(result.value().columns == query.columns);
        const auto& rows = std::get<std::vector<RecordRow>>(result.value().result);
        REQUIRE(rows.size() == 2);
        // 两行两列，行内值与处理器生成的序号一致。
        REQUIRE(rows[0].size() == 2);
        CHECK(rows[0][0] == model::Data{model::UInt32{1}});
        CHECK(rows[1][1] == model::Data{model::UInt32{2}});
    }

    SECTION("get_record_list 逐项保留 DAR 与顺序") {
        const apdu::GetRecord valid{{0x4000, 3, 0}, model::SelectAll{}, {model::Oad{0x2023, 2, 0}}};
        const apdu::GetRecord missing{{0xf001, 2, 0}, model::SelectAll{}, {}};
        auto response = sync.get_record_list({valid, missing});
        REQUIRE(static_cast<bool>(response));
        REQUIRE(response.value().records.size() == 2);
        REQUIRE(std::holds_alternative<std::vector<RecordRow>>(response.value().records[0].result));
        REQUIRE(std::holds_alternative<std::uint8_t>(response.value().records[1].result));
        CHECK(std::get<std::uint8_t>(response.value().records[1].result) == 4);
    }

    SECTION("release 完成后关联被解除，物理通道仍可用") {
        REQUIRE(static_cast<bool>(sync.release()));
        CHECK(peers.client->state() == session::State::preconnected);
        // 释放后业务事务返回 not_associated，而不是继续使用旧关联。
        auto value = sync.get({0x4000, 2, 0});
        test::require_error(value, ErrorCode::not_associated);
        // 重复释放在非关联状态下同样返回明确错误。
        test::require_error(sync.release(), ErrorCode::not_associated);
    }
}

TEST_CASE("同步 connect 的远端拒绝体现在响应原码而非本地错误", "[service][sync][connect]") {
    SECTION("协商成功时返回零结果的响应") {
        // 真实 CONNECT：预设关联已经处于 associated，无法再发起协商。
        Peers peers(/*preset=*/false);
        auto provider = make_vendor_object();
        register_vendor(*peers.registry, provider);
        peers.serve();
        CHECK(peers.client->state() == session::State::preconnected);
        SyncClientService sync(peers.client,
                               [&](std::chrono::milliseconds elapsed) { peers.drive(elapsed); });
        auto response = sync.connect();
        REQUIRE(static_cast<bool>(response));
        CHECK(response.value().result == 0);
        CHECK(peers.client->state() == session::State::associated);
    }

    SECTION("服务器拒绝时 Result 成功但响应结果非零") {
        // 服务器声明的协议版本与客户机不同，服务器以 result=5 拒绝。
        Peers peers(/*preset=*/false, /*version=*/0x0020);
        peers.serve();
        SyncClientService sync(peers.client,
                               [&](std::chrono::milliseconds elapsed) { peers.drive(elapsed); });
        auto response = sync.connect();
        // 事务本身成功，拒绝原因保留在响应的远端结果码中。
        REQUIRE(static_cast<bool>(response));
        CHECK(response.value().result == 5);
        CHECK(peers.client->state() == session::State::preconnected);
        // 被拒绝后不能进行业务事务。
        test::require_error(sync.get({0x4000, 2, 0}), ErrorCode::not_associated);
    }

    SECTION("在执行器线程内调用同步封装立即返回 busy") {
        Peers peers;
        auto provider = make_vendor_object();
        register_vendor(*peers.registry, provider);
        peers.serve();
        SyncClientService sync(peers.client,
                               [&](std::chrono::milliseconds elapsed) { peers.drive(elapsed); });
        std::optional<Result<ObjectValue>> response;
        // 回调运行在执行器中，同步等待自己的执行环境会死锁，因此必须被拒绝。
        peers.executor->post([&] { response = sync.get({0x4000, 2, 0}); });
        peers.executor->run_ready();
        REQUIRE(response);
        test::require_error(*response, ErrorCode::busy);
        CHECK(response->error().context == "sync call in executor");
    }

    SECTION("空会话在构造时被拒绝") {
        CHECK_THROWS_AS(SyncClientService(nullptr), std::invalid_argument);
    }
}

TEST_CASE("同步封装的驱动异常与超时都以本地错误结束", "[service][sync]") {
    SECTION("驱动抛异常时返回 io_error 并取消会话") {
        Peers peers;
        auto provider = make_vendor_object();
        register_vendor(*peers.registry, provider);
        peers.serve();
        SyncClientService sync(peers.client, [](std::chrono::milliseconds) {
            throw std::runtime_error("driver failure");
        });
        auto value = sync.get({0x4000, 2, 0});
        test::require_error(value, ErrorCode::io_error);
        CHECK(value.error().context == "sync driver failed");
        // 驱动失败后同步封装取消会话，事务不会悬挂。
        peers.executor->run_ready();
        CHECK(peers.client->state() == session::State::closed);
    }

    SECTION("远端无响应时按会话超时结束，不重试有副作用的事务") {
        Peers peers;
        // 服务器存在但不注册处理器：GET 仍会应答，这里改为不装配服务器服务，
        // 让请求停在通道上直到会话超时。
        SyncClientService sync(peers.client,
                               [&](std::chrono::milliseconds elapsed) { peers.drive(elapsed); });
        // 缩短事务超时以在有限次驱动内到期。
        session::SessionOptions options;
        options.preset_association = true;
        options.request_timeout = std::chrono::milliseconds{30};
        options.id_reuse_delay = std::chrono::milliseconds{30};
        auto channels = transport::MemoryChannel::pair(peers.executor);
        auto client = std::make_shared<session::Session>(channels.first, peers.executor, options);
        client->start();
        peers.executor->run_ready();
        SyncClientService slow(
            client, [&](std::chrono::milliseconds elapsed) { peers.executor->advance(elapsed); });
        auto value = slow.get({0x4000, 2, 0});
        test::require_error(value, ErrorCode::timeout);
        CHECK(value.error().context == "session transaction timeout");
        // 超时关闭物理通道，避免迟到响应被后续事务匹配。
        CHECK(client->state() == session::State::closed);
        client->close();
        peers.executor->run_ready();
    }
}

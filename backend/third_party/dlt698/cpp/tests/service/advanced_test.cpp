#include <dlt698/common/md5.hpp>
#include <dlt698/service/advanced.hpp>
#include <dlt698/standard/catalog.hpp>
#include <dlt698/transport/memory.hpp>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::service;
namespace apdu = protocol::apdu;
using namespace std::chrono_literals;

namespace {
struct Peers {
    std::shared_ptr<ManualExecutor> executor = std::make_shared<ManualExecutor>();
    std::shared_ptr<transport::MemoryChannel> near, far;
    std::shared_ptr<session::Session> client, server;
    std::shared_ptr<ObjectRegistry> objects = std::make_shared<ObjectRegistry>();
    std::shared_ptr<MemoryObject> object = std::make_shared<MemoryObject>();
    unsigned calls = 0;

    explicit Peers(std::shared_ptr<security::IBackend> client_security = {},
                   std::shared_ptr<security::IBackend> server_security = {}) {
        auto channels = transport::MemoryChannel::pair(executor);
        near = channels.first;
        far = channels.second;
        session::SessionOptions c, s;
        c.preset_association = s.preset_association = !client_security && !server_security;
        c.security_backend = std::move(client_security);
        s.security_backend = std::move(server_security);
        s.role = session::Role::server;
        client = std::make_shared<session::Session>(near, executor, c);
        server = std::make_shared<session::Session>(far, executor, s);
        object->set(2, model::UInt16{7});
        object->bind_method(1, [this](const model::Omd&, const model::Data&) {
            ++calls;
            object->set(2, model::UInt16{99});
            return ActionValue{0, model::Data{model::Null{}}};
        });
        REQUIRE(objects->register_object(
            ObjectSchema{0x4500,
                         "测试对象",
                         {{2, model::DataType::uint16, true, true}},
                         {{1, model::DataType::null, model::DataType::null}}},
            object));
        ServerService basic(server, objects);
        client->start();
        server->start();
        executor->run_ready();
    }

    ~Peers() {
        client->close();
        server->close();
        executor->run_ready();
    }

    void advanced(AdvancedServiceOptions options = {}) {
        AdvancedService backend(server, objects, executor, std::move(options));
        executor->run_ready();
    }

    std::optional<Result<apdu::Apdu>> exchange(apdu::Apdu request) {
        auto result = std::make_shared<std::optional<Result<apdu::Apdu>>>();
        client->async_exchange(std::move(request),
                               [result](Result<apdu::Apdu> r) { *result = std::move(r); });
        executor->run_ready();
        return *result;
    }
};

// 测试替身只用于检查 Session 是否调用安全后端；固定字节不具备密码学安全性。
struct TestEsam final : security::IBackend {
    unsigned resets = 0, opens = 0, protects = 0;
    bool reject_connect = false, reject_open = false, bad_response = false;

    Result<apdu::ConnectMechanism> begin_connect() override {
        return apdu::ConnectMechanism{apdu::SymmetrySecurity{{0xaa}, {0xbb}}};
    }

    Result<security::AuthenticationResult> accept_connect(const apdu::ConnectRequest& q) override {
        if (reject_connect || !std::holds_alternative<apdu::SymmetrySecurity>(q.mechanism))
            return security::AuthenticationResult{3, {}};
        return security::AuthenticationResult{0, apdu::SecurityData{{0xcc}, {0xdd}}};
    }

    Result<void> verify_connect(const apdu::ConnectRequest&,
                                const apdu::ConnectResponse& r) override {
        if (!r.security || r.security->signature != Bytes{0xdd} || bad_response)
            return Error{ErrorCode::association_failed, 0, "test authentication rejected"};
        return {};
    }

    Result<apdu::SecurityRequest> protect_request(ByteView b) override {
        ++protects;
        return apdu::SecurityRequest{false, Bytes(b.data(), b.data() + b.size()),
                                     model::Rn{{0xcc}}};
    }

    Result<Bytes> open_request(const apdu::SecurityRequest& q) override {
        ++opens;
        if (reject_open || !std::holds_alternative<model::Rn>(q.verification) ||
            std::get<model::Rn>(q.verification).value != Bytes{0xcc})
            return Error{ErrorCode::association_failed, 0, "test verification rejected"};
        return q.application;
    }

    Result<apdu::SecurityResponse> protect_response(ByteView b) override {
        ++protects;
        Bytes bytes(b.data(), b.data() + b.size());
        for (auto& byte : bytes) byte ^= 0xaa;
        return apdu::SecurityResponse{true, bytes,
                                      std::variant<model::Mac, model::SidMac>{model::Mac{{0xbb}}}};
    }

    Result<Bytes> open_response(const apdu::SecurityResponse& r) override {
        ++opens;
        if (reject_open || !r.encrypted || !r.verification ||
            !std::holds_alternative<model::Mac>(*r.verification) ||
            std::get<model::Mac>(*r.verification).value != Bytes{0xbb})
            return Error{ErrorCode::association_failed, 0, "test response rejected"};
        auto bytes = std::get<Bytes>(r.application);
        for (auto& byte : bytes) byte ^= 0xaa;
        return bytes;
    }

    void reset() noexcept override { ++resets; }
};
}  // namespace

TEST_CASE("安全后端工厂与独占生命周期", "[advanced][security]") {
    auto executor = std::make_shared<ManualExecutor>();
    auto channels = transport::MemoryChannel::pair(executor);
    session::SessionOptions options;
    unsigned created = 0;
    std::vector<std::shared_ptr<TestEsam>> backends;
    options.security_backend_factory = [&] {
        ++created;
        auto backend = std::make_shared<TestEsam>();
        backends.push_back(backend);
        return backend;
    };
    REQUIRE(session::validate_options(options));
    CHECK(created == 0);
    auto first = std::make_shared<session::Session>(channels.first, executor, options);
    auto second = std::make_shared<session::Session>(channels.second, executor, options);
    CHECK(created == 2);
    REQUIRE(backends.size() == 2);
    CHECK(backends[0] != backends[1]);
    first.reset();
    CHECK(backends[0]->resets == 1);
    CHECK(backends[1]->resets == 0);

    options.security_backend_factory = {};
    options.security_backend = backends[1];
    auto other = transport::MemoryChannel::pair(executor);
    CHECK_THROWS_AS(session::Session(other.first, executor, options), std::invalid_argument);
    CHECK(backends[1]->resets == 0);
    second.reset();
    CHECK(backends[1]->resets == 1);
    CHECK_NOTHROW(session::Session(other.first, executor, options));

    options.security_backend_factory = [] { return std::shared_ptr<security::IBackend>{}; };
    CHECK_FALSE(session::validate_options(options));
    options.security_backend = {};
    CHECK_THROWS_AS(session::Session(other.second, executor, options), std::invalid_argument);
    options.preset_association = true;
    CHECK_FALSE(session::validate_options(options));
}

TEST_CASE("ThenGet 用虚拟时间依次执行副作用和读取且默认延时生效", "[advanced][service]") {
    Peers p;
    AdvancedServiceOptions options;
    options.default_read_delay_seconds = 1;
    p.advanced(options);
    apdu::SetThenGetRequest q;
    q.items = {{{{0x4500, 2, 0}, model::UInt16{8}}, {0x4500, 2, 0}, 2},
               {{{0x4500, 2, 0}, model::UInt16{9}}, {0x4500, 2, 0}, 0}};
    std::optional<Result<apdu::Apdu>> result;
    p.client->async_exchange(q, [&](Result<apdu::Apdu> r) { result = std::move(r); });
    p.executor->run_ready();
    CHECK_FALSE(result);
    CHECK(std::get<model::Data>(p.objects->read({0x4500, 2, 0})).as<model::UInt16>().value == 8);
    p.executor->advance(1999ms);
    CHECK_FALSE(result);
    p.executor->advance(1ms);
    CHECK_FALSE(result);
    CHECK(std::get<model::Data>(p.objects->read({0x4500, 2, 0})).as<model::UInt16>().value == 9);
    p.executor->advance(1s);
    REQUIRE(result);
    REQUIRE(*result);
    const auto& r = std::get<apdu::SetThenGetResponse>(result->value());
    REQUIRE(r.items.size() == 2);
    CHECK(std::get<model::Data>(r.items[0].read.result).as<model::UInt16>().value == 8);
    CHECK(std::get<model::Data>(r.items[1].read.result).as<model::UInt16>().value == 9);
}

TEST_CASE("ACTION ThenGet 保留可选 NULL 结果且写失败仍返回独立读取结果", "[advanced][service]") {
    Peers p;
    p.advanced();
    auto result = p.exchange(
        apdu::ActionThenGetRequest{0, {{{{0x4500, 1, 0}, model::Null{}}, {0x4500, 2, 0}, 0}}, {}});
    REQUIRE(result);
    REQUIRE(*result);
    const auto& r = std::get<apdu::ActionThenGetResponse>(result->value());
    CHECK(p.calls == 1);
    REQUIRE(r.items[0].action.data);
    CHECK(r.items[0].action.data->type() == model::DataType::null);
    CHECK(std::get<model::Data>(r.items[0].read.result).as<model::UInt16>().value == 99);
    result = p.exchange(
        apdu::SetThenGetRequest{0, {{{{0x4500, 2, 0}, model::UInt8{1}}, {0x4500, 2, 0}, 0}}, {}});
    REQUIRE(result);
    REQUIRE(*result);
    CHECK(std::get<apdu::SetThenGetResponse>(result->value()).items[0].set.dar == 7);
}

TEST_CASE("关闭会话取消延时后续项且高级后端重复完成不会污染事务", "[advanced][lifetime]") {
    Peers p;
    p.advanced();
    apdu::ActionThenGetRequest q{0,
                                 {{{{0x4500, 1, 0}, model::Null{}}, {0x4500, 2, 0}, 3},
                                  {{{0x4500, 1, 0}, model::Null{}}, {0x4500, 2, 0}, 0}},
                                 {}};
    unsigned completed = 0;
    p.client->async_exchange(q, [&](Result<apdu::Apdu> r) {
        CHECK_FALSE(r);
        ++completed;
    });
    p.executor->run_ready();
    CHECK(p.calls == 1);
    p.client->close();
    p.executor->run_ready();
    p.executor->advance(4s);
    CHECK(p.calls == 1);
    CHECK(completed == 1);
}

TEST_CASE("GET MD5 服务摘要来源于 Data 编码而未知对象保留 DAR", "[advanced][service]") {
    Peers p;
    p.advanced();
    auto result = p.exchange(apdu::GetMd5Request{0, {0x4500, 2, 0}, {}});
    REQUIRE(result);
    REQUIRE(*result);
    const auto& r = std::get<apdu::GetMd5Response>(result->value());
    const auto bytes = test::hex("12 00 07");
    CHECK(std::get<std::array<std::uint8_t, 16>>(r.result) == md5(bytes));
    result = p.exchange(apdu::GetMd5Request{0, {0xffff, 2, 0}, {}});
    REQUIRE(result);
    REQUIRE(*result);
    CHECK(std::get<std::uint8_t>(std::get<apdu::GetMd5Response>(result->value()).result) == 4);
}

TEST_CASE("REPORT 双向确认不抢占客户机 GET 且重发只交付一次", "[advanced][report]") {
    Peers p;
    unsigned reports = 0, acknowledged = 0, gets = 0, demands = 0;
    p.client->set_acd_handler([&] { ++demands; });
    p.server->set_access_demand(true);
    p.client->set_report_handler([&](const apdu::ReportNotification&) {
        ++reports;
        return true;
    });
    apdu::ReportNotification q;
    q.payload = std::vector<apdu::AttributeResult>{{{0x4500, 2, 0}, model::UInt16{7}}};
    p.server->async_exchange(q, [&](Result<apdu::Apdu> r) {
        REQUIRE(r);
        CHECK(std::holds_alternative<apdu::ReportResponse>(r.value()));
        ++acknowledged;
    });
    p.client->async_get({{0x4500, 2, 0}}, false, [&](Result<apdu::GetResponse> r) {
        REQUIRE(r);
        ++gets;
    });
    p.executor->run_ready();
    CHECK(reports == 1);
    CHECK(acknowledged == 1);
    CHECK(gets == 1);
    CHECK(demands == 2);
    q.piid_acd = 0;
    const auto bytes = apdu::encode_apdu(q);
    REQUIRE(bytes);
    const auto frame = protocol::link::encode_frame({0x83, {}, 0, bytes.value()});
    REQUIRE(frame);
    p.far->async_write(frame.value(), [](Result<void>) {});
    p.executor->run_ready();
    CHECK(reports == 1);
}

TEST_CASE("REPORT 未接受通知有限重发后关闭且透明和记录通知确认", "[advanced][report]") {
    SECTION("拒绝接收不确认") {
        Peers p;
        unsigned count = 0, completed = 0;
        p.client->set_report_handler([&](const apdu::ReportNotification&) {
            ++count;
            return false;
        });
        apdu::ReportNotification q;
        q.payload = apdu::TransData{{0xf201, 2, 0}, {{0xaa}}};
        p.server->async_exchange(q, [&](Result<apdu::Apdu> r) {
            CHECK_FALSE(r);
            CHECK(r.error().code == ErrorCode::timeout);
            ++completed;
        });
        p.executor->run_ready();
        for (unsigned i = 0; i < 3; ++i) p.executor->advance(5s);
        CHECK(count == 3);
        CHECK(completed == 1);
        CHECK(p.server->state() == session::State::closed);
    }
    SECTION("三种确认") {
        Peers p;
        p.client->set_report_handler([](const apdu::ReportNotification&) { return true; });
        for (auto payload : std::vector<decltype(apdu::ReportNotification{}.payload)>{
                 std::vector<apdu::AttributeResult>{{{0x4500, 2, 0}, std::uint8_t{4}}},
                 std::vector<apdu::RecordResult>{{{0x5004, 2, 0}, {}, std::uint8_t{4}}},
                 apdu::TransData{{0xf201, 2, 0}, {{0xaa}, {0xbb}}}}) {
            apdu::ReportNotification q;
            q.payload = payload;
            bool done = false;
            p.server->async_exchange(q, [&](Result<apdu::Apdu> r) {
                REQUIRE(r);
                done = true;
            });
            p.executor->run_ready();
            CHECK(done);
        }
    }
}

TEST_CASE("FollowReport 与 ACD 在匹配结果上交付且不隐式猜测事件 OAD", "[advanced][follow]") {
    Peers p;
    unsigned follows = 0, demands = 0;
    p.client->set_follow_handler([&](const apdu::FollowReport& v) {
        ++follows;
        CHECK(std::get<0>(v).size() == 1);
    });
    p.client->set_acd_handler([&] { ++demands; });
    p.server->set_access_demand(true);
    p.server->set_request_handler([](const apdu::GetRequest& q) {
        apdu::GetResponse r;
        r.attributes = {{q.attributes.front(), model::UInt16{7}}};
        r.follow_report = apdu::FollowReport{
            std::vector<apdu::AttributeResult>{{{0x4500, 2, 0}, model::UInt16{8}}}};
        return r;
    });
    bool done = false;
    p.client->async_get({{0x4500, 2, 0}}, false, [&](Result<apdu::GetResponse> r) {
        REQUIRE(r);
        REQUIRE(r.value().follow_report);
        done = true;
    });
    p.executor->run_ready();
    CHECK(done);
    CHECK(follows == 1);
    CHECK(demands == 1);
}

TEST_CASE("PROXY 路由独立设备会话并执行普通列表记录和 ThenGet", "[advanced][proxy]") {
    Peers p, target;
    target.advanced();
    target.object->bind_record(3, [](const apdu::GetRecord& q) {
        return apdu::RecordResult{q.attribute, q.columns, std::uint8_t{4}};
    });
    auto router = std::make_shared<ProxyRouter>();
    const model::Tsa tsa{{0x00, 0x01}};
    REQUIRE(router->bind(tsa, target.client));
    AdvancedServiceOptions options;
    options.proxy = router;
    p.advanced(options);
    std::vector<apdu::ProxyRequestPayload> payloads{
        std::vector<apdu::ProxyTarget<model::Oad>>{{tsa, 1, {{0x4500, 2, 0}}}},
        apdu::ProxyRecordRequest{tsa, {{0x4500, 3, 0}, {}, {}}},
        std::vector<apdu::ProxyTarget<apdu::SetAttribute>>{
            {tsa, 1, {{{0x4500, 2, 0}, model::UInt16{8}}}}},
        std::vector<apdu::ProxyTarget<apdu::SetThenGet>>{
            {tsa, 1, {{{{0x4500, 2, 0}, model::UInt16{9}}, {0x4500, 2, 0}, 0}}}},
        std::vector<apdu::ProxyTarget<apdu::ActionMethod>>{
            {tsa, 1, {{{0x4500, 1, 0}, model::Null{}}}}},
        std::vector<apdu::ProxyTarget<apdu::ActionThenGet>>{
            {tsa, 1, {{{{0x4500, 1, 0}, model::Null{}}, {0x4500, 2, 0}, 0}}}}};
    for (auto payload : payloads) {
        apdu::ProxyRequest q;
        q.timeout_seconds = 3;
        q.payload = payload;
        std::optional<Result<apdu::Apdu>> result;
        p.client->async_exchange(q, [&](Result<apdu::Apdu> r) { result = std::move(r); });
        for (unsigned i = 0; i < 5; ++i) {
            p.executor->run_ready();
            target.executor->run_ready();
        }
        REQUIRE(result);
        REQUIRE(*result);
        CHECK(apdu::advanced_matches(q, result->value()));
    }
    CHECK(target.calls == 2);
}

TEST_CASE("PROXY 超时取消目标并丢弃重复迟到回调，未知地址逐项 DAR", "[advanced][proxy]") {
    struct Held final : IProxyProvider {
        Handler held;
        unsigned cancels = 0;

        Cancel async_request(model::Tsa, apdu::Apdu, Handler h) override {
            held = std::move(h);
            return [this] { ++cancels; };
        }
    };

    Peers p;
    auto provider = std::make_shared<Held>();
    AdvancedServiceOptions options;
    options.proxy = provider;
    p.advanced(options);
    apdu::ProxyRequest q;
    q.timeout_seconds = 3;
    q.payload =
        std::vector<apdu::ProxyTarget<model::Oad>>{{model::Tsa{{0, 1}}, 1, {{0x4500, 2, 0}}}};
    unsigned completed = 0;
    p.client->async_exchange(q, [&](Result<apdu::Apdu> r) {
        REQUIRE(r);
        CHECK(
            std::get<std::uint8_t>(
                std::get<0>(std::get<apdu::ProxyResponse>(r.value()).payload)[0].items[0].result) ==
            2);
        ++completed;
    });
    p.executor->run_ready();
    p.executor->advance(1s);
    CHECK(completed == 1);
    CHECK(provider->cancels == 1);
    apdu::GetResponse late;
    late.list = true;
    late.attributes = {{{0x4500, 2, 0}, model::UInt16{8}}};
    provider->held(apdu::Apdu{late});
    provider->held(apdu::Apdu{late});
    p.executor->run_ready();
    CHECK(completed == 1);
}

TEST_CASE("PROXY 透明转发保留通信配置与返回字节并验证端口", "[advanced][proxy]") {
    Peers p;
    AdvancedServiceOptions options;
    options.trans = [](apdu::ProxyTransRequest q,
                       std::function<void(Result<apdu::ProxyTransResponse>)> handler) {
        CHECK(q.communication.baud == 6);
        CHECK(q.byte_timeout_milliseconds == 10);
        handler(apdu::ProxyTransResponse{q.port, q.command});
        return IProxyProvider::Cancel{};
    };
    p.advanced(options);
    apdu::ProxyRequest q;
    q.payload = apdu::ProxyTransRequest{{0xf201, 2, 1}, {}, 3, 10, {0xaa, 0xbb}};
    const auto r = p.exchange(q);
    REQUIRE(r);
    REQUIRE(*r);
    CHECK(std::get<Bytes>(std::get<6>(std::get<apdu::ProxyResponse>(r->value()).payload).result) ==
          Bytes{0xaa, 0xbb});
}

TEST_CASE("ESAM 认证与明文请求密文响应由后端处理，释放清理材料", "[advanced][security]") {
    auto c = std::make_shared<TestEsam>(), s = std::make_shared<TestEsam>();
    Peers p(c, s);
    p.advanced();
    bool connected = false;
    p.client->async_connect([&](Result<apdu::ConnectResponse> r) {
        REQUIRE(r);
        CHECK(r.value().result == 0);
        connected = true;
    });
    p.executor->run_ready();
    REQUIRE(connected);
    const auto r = p.exchange(apdu::GetMd5Request{0, {0x4500, 2, 0}, {}});
    REQUIRE(r);
    REQUIRE(*r);
    CHECK(s->opens == 1);
    CHECK(c->opens == 1);
    CHECK(c->protects == 1);
    CHECK(s->protects == 1);
    const auto then = p.exchange(
        apdu::ActionThenGetRequest{0, {{{{0x4500, 1, 0}, model::Null{}}, {0x4500, 2, 0}, 0}}, {}});
    REQUIRE(then);
    REQUIRE(*then);
    CHECK(p.calls == 1);
    unsigned reports = 0, follows = 0, demands = 0;
    p.client->set_report_handler([&](const apdu::ReportNotification&) {
        ++reports;
        return true;
    });
    p.client->set_follow_handler([&](const apdu::FollowReport&) { ++follows; });
    p.client->set_acd_handler([&] { ++demands; });
    p.server->set_access_demand(true);
    apdu::ReportNotification notification;
    notification.payload = apdu::TransData{{0xf201, 2, 0}, {{0xaa}}};
    notification.follow_report =
        apdu::FollowReport{std::vector<apdu::AttributeResult>{{{0x4500, 2, 0}, model::UInt16{99}}}};
    bool acknowledged = false;
    p.server->async_exchange(notification, [&](Result<apdu::Apdu> result) {
        REQUIRE(result);
        acknowledged = true;
    });
    p.executor->run_ready();
    CHECK(acknowledged);
    CHECK(reports == 1);
    CHECK(follows == 1);
    CHECK(demands == 1);
    CHECK(c->opens == 3);
    CHECK(s->opens == 3);
    bool released = false;
    p.client->async_release([&](Result<void> result) {
        REQUIRE(result);
        released = true;
    });
    p.executor->run_ready();
    CHECK(released);
    CHECK(c->resets >= 2);
    CHECK(s->resets >= 1);
}

TEST_CASE("ESAM 拒绝认证或验证失败不进入业务且明文降级关闭会话", "[advanced][security]") {
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        auto c = std::make_shared<TestEsam>(), s = std::make_shared<TestEsam>();
        s->reject_connect = scenario == 0;
        c->bad_response = scenario == 1;
        Peers p(c, s);
        bool done = false;
        p.client->async_connect([&](Result<apdu::ConnectResponse>) { done = true; });
        p.executor->run_ready();
        REQUIRE(done);
        if (scenario < 2) {
            CHECK(p.client->state() != session::State::associated);
            CHECK(c->resets >= 2);
            CHECK(s->resets >= 1);
            continue;
        }
        s->reject_open = scenario == 2;
        const auto ap =
            apdu::encode_apdu(apdu::ActionRequest{0, false, {{{0x4500, 1, 0}, model::Null{}}}, {}});
        REQUIRE(ap);
        Bytes bytes = ap.value();
        if (scenario == 2) {
            const auto outer = c->protect_request(bytes);
            REQUIRE(outer);
            const auto wire = apdu::encode_security(outer.value());
            REQUIRE(wire);
            bytes = wire.value();
        }
        const auto frame = protocol::link::encode_frame({0x43, {}, 0, bytes});
        REQUIRE(frame);
        p.near->async_write(frame.value(), [](Result<void>) {});
        p.executor->run_ready();
        CHECK(p.calls == 0);
        CHECK(p.server->state() == session::State::closed);
    }
}

TEST_CASE("ESAM 和端口标准对象校验结构枚举及 COMDCB", "[advanced][catalog]") {
    CHECK(standard::objects().size() == 131);
    CHECK(standard::validate_value(
        {0xf100, 7, 0}, model::Structure{{model::UInt32{1}, model::UInt32{2}, model::UInt32{3}}}));
    CHECK_FALSE(standard::validate_value(
        {0xf100, 7, 0}, model::Structure{{model::UInt16{1}, model::UInt32{2}, model::UInt32{3}}}));
    const auto value = model::Array{
        {model::Structure{{model::VisibleString{"RS485-1"}, model::Comdcb{}, model::Enum{1}}}}};
    CHECK(standard::validate_value({0xf201, 2, 0}, value));
    CHECK(standard::make_oad(0xf201, 2, 1));
    CHECK_FALSE(standard::validate_value({0xf101, 2, 0}, model::Enum{2}));
}

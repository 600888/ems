#include <dlt698/dlt698.hpp>
#include <dlt698/service/memory_records.hpp>
#include <dlt698/service/point_probe.hpp>
#include <dlt698/service/service.hpp>
#include <dlt698/transport/memory.hpp>

#include "catch/test_support.hpp"

using namespace dlt698;
namespace oi = standard::oi;
namespace apdu = protocol::apdu;
using apdu::RecordRow;
using model::Oad;

namespace {
// 测试日期直接保存标准的七字节值，不经过系统时区或字符串解析。
model::DateTimeS day(std::uint8_t value) { return {{0x07, 0xea, 10, value, 0, 0, 0}}; }

std::vector<Oad> freeze_columns() {
    return {
        {oi::freeze_sequence, 2, 0}, {oi::freeze_time, 2, 0}, {oi::forward_active_energy, 2, 1}};
}

std::vector<RecordRow> freeze_rows(std::uint32_t count = 3, std::uint32_t offset = 1000) {
    std::vector<RecordRow> rows;
    for (std::uint32_t i = 1; i <= count; ++i)
        rows.push_back(
            {model::UInt32{i}, day(static_cast<std::uint8_t>(i)), model::UInt32{offset + i}});
    return rows;
}

const std::vector<RecordRow>& rows_of(const apdu::RecordResult& result) {
    REQUIRE(std::holds_alternative<std::vector<RecordRow>>(result.result));
    return std::get<std::vector<RecordRow>>(result.result);
}

std::uint8_t dar_of(const apdu::RecordResult& result) {
    REQUIRE(std::holds_alternative<std::uint8_t>(result.result));
    return std::get<std::uint8_t>(result.result);
}

standard::Capabilities support(std::uint8_t protocol = 0xf2) {
    apdu::AssociationParameters parameters;
    parameters.protocol[0] = protocol;
    return {parameters};
}

// 真实 CONNECT，业务位只由应用明确声明；销毁前驱动所有关闭回调。
struct Peers {
    std::shared_ptr<ManualExecutor> executor = std::make_shared<ManualExecutor>();
    std::shared_ptr<session::Session> client, server;

    Peers(bool connect = true, std::uint16_t apdu_size = 1024, std::uint8_t function = 0) {
        auto channels = transport::MemoryChannel::pair(executor);
        session::SessionOptions c, s;
        s.role = session::Role::server;
        c.parameters.function[0] = function ? static_cast<std::uint8_t>(function | 0x40) : 0;
        s.parameters.function[0] = function;
        c.parameters.apdu_bytes = s.parameters.apdu_bytes = apdu_size;
        c.preset_association = s.preset_association = !connect;
        client = std::make_shared<session::Session>(channels.first, executor, c);
        server = std::make_shared<session::Session>(channels.second, executor, s);
        client->start();
        server->start();
    }

    ~Peers() {
        client->close();
        server->close();
        executor->run_ready();
    }

    standard::Capabilities connect() {
        std::optional<Result<apdu::ConnectResponse>> response;
        client->async_connect([&](auto value) { response = std::move(value); });
        executor->run_ready();
        REQUIRE(response);
        INFO((!*response ? response->error().context : "connect success"));
        REQUIRE(*response);
        auto result = standard::capabilities_from_connect(response->value());
        REQUIRE(result);
        return result.value();
    }
};
}  // namespace

TEST_CASE("标准记录模板、列定义和独立报文", "[standard][record]") {
    CHECK(standard::objects().size() == 131);
    for (auto identifier : {oi::daily_freeze, oi::monthly_freeze, oi::meter_power_down_event,
                            oi::terminal_initialization_event}) {
        REQUIRE(standard::find_record(identifier));
        auto schema = service::make_object_schema(identifier, {2});
        REQUIRE(schema);
        CHECK(schema.value().attributes[0].record);
        CHECK_FALSE(schema.value().attributes[0].writable);
        CHECK_FALSE(standard::validate_value({identifier, 2, 0}, model::Array{}));
    }
    CHECK_FALSE(standard::find_object(oi::event_source));
    CHECK_FALSE(standard::find_record(oi::voltage));
    CHECK(standard::find_object(oi::daily_freeze)->class_id == 9);
    CHECK(standard::find_object(oi::meter_power_down_event)->class_id == 7);
    // 附录 D.3：日冻结以冻结时间定位，列为正向有功电能；此向量独立于编码器构造。
    const model::DateTimeS time{{0x07, 0xe0, 1, 20, 0, 0, 0}};
    auto query = standard::record_at(oi::daily_freeze, time, {{oi::forward_active_energy, 2, 0}});
    REQUIRE(query);
    auto encoded = apdu::encode_get(apdu::GetRecordRequest{4, false, {query.value()}, {}});
    REQUIRE(encoded);
    CHECK(encoded.value() ==
          test::hex(
              "05 03 04 50 04 02 00 01 20 21 02 00 1C 07 E0 01 14 00 00 00 01 00 00 10 02 00 00"));
    CHECK(standard::record_between(oi::monthly_freeze, day(1), day(3)));
    CHECK_FALSE(standard::record_between(oi::daily_freeze, day(3), day(3)));
    CHECK_FALSE(standard::record_sequences(oi::daily_freeze, 3, 1));
    CHECK(standard::validate_record_time({{0x07, 0xe8, 2, 29, 23, 59, 59}}));
    CHECK_FALSE(standard::validate_record_time({{0x07, 0xe9, 2, 29, 0, 0, 0}}));
    CHECK_FALSE(standard::validate_record_time({{0x07, 0xea, 10, 1, 0xff, 0, 0}}));
    auto duplicate = standard::make_record_query(
        oi::daily_freeze, model::SelectAll{}, {{oi::freeze_time, 2, 0}, {oi::freeze_time, 2, 0}});
    CHECK_FALSE(duplicate);
    auto road = query.value();
    road.columns = {model::Road{{oi::forward_active_energy, 2, 0}, {}}};
    CHECK(standard::validate_record_query(road).error().code == ErrorCode::unsupported_service);
    Limits limits;
    limits.max_elements = 1;
    CHECK_FALSE(standard::make_record_query(oi::daily_freeze, model::SelectAll{}, freeze_columns(),
                                            {}, limits));
}

TEST_CASE("内存记录行列选择、空结果和资源上限", "[standard][record]") {
    auto created = service::MemoryRecords::create(oi::daily_freeze, freeze_columns());
    REQUIRE(created);
    auto memory = created.value();
    auto all = standard::make_record_query(oi::daily_freeze, model::SelectAll{}).value();
    auto empty = memory->read_record(all);
    CHECK(rows_of(empty).empty());
    CHECK(empty.columns.size() == 3);
    REQUIRE(memory->replace_rows(freeze_rows()));
    auto selected =
        standard::record_between(oi::daily_freeze, day(1), day(3),
                                 {{oi::forward_active_energy, 2, 1}, {oi::freeze_sequence, 2, 0}})
            .value();
    const auto response = memory->read_record(selected);
    CHECK(response.columns == selected.columns);
    REQUIRE(rows_of(response).size() == 2);
    CHECK(rows_of(response)[0][0] == model::Data{model::UInt32{1001}});
    CHECK(rows_of(response)[1][1] == model::Data{model::UInt32{2}});
    auto sequence = standard::record_sequences(oi::daily_freeze, 2, 4).value();
    CHECK(rows_of(memory->read_record(sequence)).size() == 2);
    auto exact = standard::record_at(oi::daily_freeze, day(2)).value();
    CHECK(rows_of(memory->read_record(exact)).size() == 1);
    exact = standard::record_at(oi::daily_freeze, day(10)).value();
    CHECK(rows_of(memory->read_record(exact)).empty());
    auto previous = standard::make_record_query(oi::daily_freeze, model::Selector9{1}).value();
    CHECK(rows_of(memory->read_record(previous))[0][0] == model::Data{model::UInt32{3}});
    previous.rows = model::Selector9{4};
    CHECK(rows_of(memory->read_record(previous)).empty());
    previous.rows = model::Selector9{0};
    CHECK(dar_of(memory->read_record(previous)) == 8);
    selected.rows = model::Selector2{{oi::freeze_time, 2, 0}, day(1), day(3), model::Ti{0, 1}};
    CHECK(dar_of(memory->read_record(selected)) == 3);
    selected.rows = model::Selector1{{oi::freeze_time, 2, 0}, model::UInt32{1}};
    CHECK(dar_of(memory->read_record(selected)) == 8);
    selected =
        standard::make_record_query(oi::daily_freeze, model::SelectAll{}, {{oi::voltage, 2, 1}})
            .value();
    CHECK(dar_of(memory->read_record(selected)) == 4);
    auto bad = freeze_rows();
    bad[1][0] = model::UInt32{1};
    CHECK_FALSE(memory->replace_rows(bad));
    bad = freeze_rows();
    bad[0][1] = day(32);
    CHECK_FALSE(memory->replace_rows(bad));
    bad = freeze_rows();
    bad[0].pop_back();
    CHECK_FALSE(memory->replace_rows(bad));
    bad = freeze_rows();
    bad[0][2] = model::UInt16{100};
    CHECK_FALSE(memory->replace_rows(bad));
    CHECK(rows_of(memory->read_record(all)).size() == 3);
    REQUIRE(memory->replace_rows(freeze_rows(1, 5000)));
    // 已返回的快照独立于后来替换的数据集。
    CHECK(rows_of(response)[0][0] == model::Data{model::UInt32{1001}});
    CHECK(std::get<std::uint8_t>(memory->read(all.attribute)) == 5);
    service::RecordLimits budget;
    budget.max_result_rows = 2;
    auto bounded =
        service::MemoryRecords::create(oi::daily_freeze, freeze_columns(), {}, {}, budget).value();
    REQUIRE(bounded->replace_rows(freeze_rows()));
    CHECK(dar_of(bounded->read_record(all)) == 3);
    budget.max_rows = 1;
    bounded =
        service::MemoryRecords::create(oi::daily_freeze, freeze_columns(), {}, {}, budget).value();
    CHECK_FALSE(bounded->replace_rows(freeze_rows()));
    budget.max_snapshot_bytes = 1;
    bounded =
        service::MemoryRecords::create(oi::daily_freeze, freeze_columns(), {}, {}, budget).value();
    CHECK_FALSE(bounded->replace_rows(freeze_rows(1)));
    budget.max_columns = 0;
    CHECK_FALSE(service::MemoryRecords::create(oi::daily_freeze, {}, {}, {}, budget));
    CHECK_FALSE(service::MemoryRecords::create(oi::daily_freeze, {{oi::freeze_time, 2, 0}}));
}

TEST_CASE("常用事件来源与标准 provider 的记录防线", "[standard][record]") {
    for (auto identifier : {oi::meter_power_down_event, oi::terminal_initialization_event}) {
        auto memory = service::MemoryRecords::create(identifier).value();
        const model::DateTimeS unspecified{{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
        std::vector<RecordRow> event{{model::UInt32{1}, day(1), unspecified, model::Null{}}};
        REQUIRE(memory->replace_rows(event));
        event[0][3] = model::UInt8{1};
        CHECK_FALSE(memory->replace_rows(event));
        service::ObjectRegistry registry;
        REQUIRE(service::register_standard_object(registry, identifier, {2}, memory));
        auto query = standard::record_sequences(identifier, 1, 2).value();
        CHECK(rows_of(registry.read_record(query)).size() == 1);
    }
    service::ObjectRegistry registry;
    auto provider = std::make_shared<service::MemoryObject>();
    REQUIRE(service::register_standard_object(registry, oi::daily_freeze, {2}, provider));
    auto query = standard::make_record_query(oi::daily_freeze, model::SelectAll{},
                                             {{oi::freeze_sequence, 2, 0}})
                     .value();
    CHECK(dar_of(registry.read_record(query)) == 4);
    provider->bind_record(2, [](const auto& q) {
        return apdu::RecordResult{q.attribute, q.columns,
                                  std::vector<RecordRow>{{model::UInt16{1}}}};
    });
    CHECK(dar_of(registry.read_record(query)) == 7);
    provider->bind_record(2, [](const auto& q) {
        return apdu::RecordResult{q.attribute, q.columns, std::uint8_t{3}};
    });
    CHECK(dar_of(registry.read_record(query)) == 3);
    provider->bind_record(
        2, [](const auto&) -> apdu::RecordResult { throw std::runtime_error("provider failure"); });
    CHECK(dar_of(registry.read_record(query)) == 255);
    auto ordinary = std::make_shared<service::MemoryObject>();
    REQUIRE(service::register_standard_object(registry, oi::frequency, {2}, ordinary));
    CHECK(dar_of(registry.read_record({{oi::frequency, 2, 0}, model::SelectAll{}, {}})) == 5);
    service::ObjectRegistry bounded;
    Limits limits;
    limits.max_elements = 1;
    REQUIRE(
        service::register_standard_object(bounded, oi::daily_freeze, {2}, provider, {}, limits));
    query.columns.push_back(Oad{oi::freeze_time, 2, 0});
    CHECK(dar_of(bounded.read_record(query)) == 3);
}

TEST_CASE("一致性位图给出提示并规划 List 或 Normal", "[standard][capabilities]") {
    const std::vector<Oad> points{{oi::forward_active_energy, 2, 1},
                                  {oi::voltage_harmonics, 2, 1},
                                  {oi::daily_freeze, 2, 0},
                                  {0xf001, 2, 0}};
    auto caps = support();
    CHECK(standard::candidate_points(caps, points, true).size() == points.size());
    CHECK(standard::point_support(caps, points[0]) == standard::Support::unknown);
    caps.negotiated->function.back() = 0xff;
    CHECK(standard::point_support(caps, points[0]) == standard::Support::unknown);
    CHECK(standard::candidate_points(caps, points, true).size() == points.size());
    caps.negotiated->function[0] = 0x80;  // C.2 第零位对应电能，MSB 先行。
    CHECK(standard::point_support(caps, points[0]) == standard::Support::yes);
    CHECK(standard::point_support(caps, points[1]) == standard::Support::no);
    CHECK(standard::point_support(caps, {oi::reverse_active_energy, 2, 1}) ==
          standard::Support::no);
    CHECK(standard::point_support(caps, {oi::forward_active_energy, 2, 2}) ==
          standard::Support::no);
    CHECK(standard::candidate_points(caps, points).size() == 4);
    CHECK(standard::candidate_points(caps, points, true).size() == 3);
    auto batches = standard::plan_reads(caps, points, 3);
    REQUIRE(batches);
    CHECK(batches.value().size() == 2);
    CHECK(batches.value()[0].list);
    caps.negotiated->protocol[0] = 0xc0;
    batches = standard::plan_reads(caps, points);
    REQUIRE(batches);
    CHECK(batches.value().size() == points.size());
    CHECK_FALSE(batches.value()[0].list);
    CHECK_FALSE(standard::require_record_service(caps));
    caps.negotiated->protocol[0] = 0x80;
    CHECK_FALSE(standard::plan_reads(caps, points));
    CHECK(standard::require_record_service({}));
    CHECK(standard::plan_reads({}, points).value().size() == points.size());
    CHECK_FALSE(standard::plan_reads({}, {}));
    CHECK_FALSE(standard::plan_reads({}, points, 0));
    apdu::ConnectResponse denied;
    denied.result = 3;
    auto rejected = standard::capabilities_from_connect(denied);
    CHECK(rejected.error().remote_code == 3);
    denied.result = 0;
    CHECK_FALSE(standard::capabilities_from_connect(denied));
    Peers peers(true, 1024, 0x80);
    caps = peers.connect();
    CHECK(caps.negotiated->function[0] == 0x80);
    CHECK(standard::service_support(caps, standard::ReadService::record) == standard::Support::yes);
}

TEST_CASE("真实记录事务收齐分块且保留首次快照", "[standard][record][session]") {
    Peers peers(true, 80);
    peers.connect();
    auto memory = service::MemoryRecords::create(oi::daily_freeze, freeze_columns()).value();
    REQUIRE(memory->replace_rows(freeze_rows(12)));
    auto registry = std::make_shared<service::ObjectRegistry>();
    REQUIRE(service::register_standard_object(*registry, oi::daily_freeze, {2}, memory));
    unsigned provider_calls = 0;
    peers.server->set_record_handler([&](const apdu::GetRecordRequest& request) {
        ++provider_calls;
        auto result = registry->read_record(request.records[0]);
        // 下一任务在第一响应块前替换后端，后续 Next 仍须使用 Session 保存的旧结果。
        peers.executor->post([memory] { REQUIRE(memory->replace_rows(freeze_rows(1, 9000))); });
        return apdu::GetRecordResponse{request.piid, request.list, {result}, {}};
    });
    auto query = standard::make_record_query(oi::daily_freeze, model::SelectAll{}).value();
    std::optional<Result<apdu::RecordResult>> response;
    service::ClientService client(peers.client);
    client.async_get_record(query, [&](auto result) { response = std::move(result); });
    peers.executor->run_ready();
    REQUIRE(response);
    INFO((!*response ? response->error().context : "record success"));
    REQUIRE(*response);
    CHECK(provider_calls == 1);
    REQUIRE(rows_of(response->value()).size() == 12);
    CHECK(rows_of(response->value())[11][2] == model::Data{model::UInt32{1012}});
    CHECK(rows_of(memory->read_record(query)).size() == 1);
}

TEST_CASE("点位探测逐项保留 Data、DAR 和本地类型错误", "[standard][probe]") {
    Peers peers;
    auto caps = peers.connect();
    unsigned requests = 0;
    peers.server->set_request_handler([&](const apdu::GetRequest& request) {
        ++requests;
        apdu::GetResponse result{request.piid, request.list, {}, {}};
        for (const auto& attribute : request.attributes) {
            if (attribute.oi == oi::frequency)
                result.attributes.push_back({attribute, model::UInt16{5000}});
            else if (attribute.oi == oi::voltage)
                result.attributes.push_back({attribute, model::UInt8{1}});
            else if (attribute.oi == 0xf001)
                result.attributes.push_back({attribute, model::OctetString{{1}}});
            else
                result.attributes.push_back({attribute, std::uint8_t{4}});
        }
        return result;
    });
    peers.executor->run_ready();
    CHECK(requests == 0);
    const std::vector<Oad> points{{oi::frequency, 2, 0},
                                  {oi::voltage, 2, 1},
                                  {0xf001, 2, 0},
                                  {0xf002, 2, 0},
                                  {oi::frequency, 2, 0}};
    service::ProbeOptions options;
    options.batch_size = 2;
    std::optional<Result<std::vector<service::PointResult>>> response;
    unsigned callbacks = 0;
    service::async_probe_points(peers.client, caps, points, options, [&](auto result) {
        ++callbacks;
        response = std::move(result);
    });
    peers.executor->run_ready();
    REQUIRE(response);
    REQUIRE(*response);
    const auto& results = response->value();
    REQUIRE(results.size() == points.size());
    for (std::size_t i = 0; i < points.size(); ++i) CHECK(results[i].attribute == points[i]);
    CHECK(callbacks == 1);
    CHECK(requests == 3);
    CHECK(results[0].schema_checked);
    CHECK_FALSE(results[0].validation_error);
    CHECK(std::holds_alternative<model::Data>(results[1].outcome));
    CHECK(results[1].validation_error);
    CHECK_FALSE(results[2].schema_checked);
    CHECK(std::get<std::uint8_t>(results[3].outcome) == 4);
    // 仅业务提示为 no 仍会显式读取；C.1 不支持 List 则实际线上拆 Normal。
    caps.negotiated->protocol[0] = 0xc0;
    response.reset();
    requests = 0;
    service::async_probe_points(peers.client, caps, points, options,
                                [&](auto result) { response = std::move(result); });
    peers.executor->run_ready();
    REQUIRE(response);
    REQUIRE(*response);
    CHECK(requests == points.size());
    peers.client->close();
    peers.executor->run_ready();
    response.reset();
    service::async_probe_points(peers.client, caps, points, options,
                                [&](auto result) { response = std::move(result); });
    peers.executor->run_ready();
    REQUIRE(response);
    REQUIRE(*response);
    CHECK(std::get<Error>(response->value()[0].outcome).code == ErrorCode::closed);
}

TEST_CASE("重复记录 OAD 查询维持各次结果边界", "[standard][record][session]") {
    Peers peers(true, 80);
    peers.connect();
    auto memory = service::MemoryRecords::create(oi::daily_freeze, freeze_columns()).value();
    REQUIRE(memory->replace_rows(freeze_rows()));
    auto registry = std::make_shared<service::ObjectRegistry>();
    REQUIRE(service::register_standard_object(*registry, oi::daily_freeze, {2}, memory));
    service::ServerService server(peers.server, registry);
    service::ClientService client(peers.client);
    std::optional<Result<apdu::GetRecordResponse>> response;
    client.async_get_record_list({standard::record_at(oi::daily_freeze, day(1)).value(),
                                  standard::record_at(oi::daily_freeze, day(2)).value()},
                                 [&](auto result) { response = std::move(result); });
    peers.executor->run_ready();
    REQUIRE(response);
    REQUIRE(*response);
    REQUIRE(response->value().records.size() == 2);
    CHECK(rows_of(response->value().records[0])[0][0] == model::Data{model::UInt32{1}});
    CHECK(rows_of(response->value().records[1])[0][0] == model::Data{model::UInt32{2}});
}

TEST_CASE("记录按行分块的表头、顺序和累计预算", "[standard][record][blocks]") {
    apdu::GetRecordResponse source{2, true, {}, {}};
    source.records.push_back({{oi::daily_freeze, 2, 0},
                              {Oad{oi::freeze_sequence, 2, 0}, Oad{oi::freeze_time, 2, 0},
                               Oad{oi::forward_active_energy, 2, 1}},
                              freeze_rows(12)});
    source.records.push_back(
        {{oi::monthly_freeze, 2, 0}, {Oad{oi::freeze_sequence, 2, 0}}, std::vector<RecordRow>{}});
    source.records.push_back({{oi::meter_power_down_event, 2, 0}, {}, std::uint8_t{4}});
    auto split = apdu::GetBlockTransfer::split(source, 80, {});
    REQUIRE(split);
    REQUIRE(split.value().size() > 3);
    apdu::GetBlockTransfer collector(2, true, {});
    for (std::size_t i = 0; i < split.value().size(); ++i) {
        const auto& block = split.value()[i];
        CHECK(block.block == i);
        CHECK(block.last == (i + 1 == split.value().size()));
        auto encoded = apdu::encode_get(block);
        REQUIRE(encoded);
        CHECK(encoded.value().size() <= 80);
        auto accepted = collector.accept(block);
        REQUIRE(accepted);
        if (block.last) {
            REQUIRE(accepted.value());
            const auto& result = std::get<apdu::GetRecordResponse>(*accepted.value());
            REQUIRE(result.records.size() == 3);
            CHECK(rows_of(result.records[0]) == freeze_rows(12));
            CHECK(rows_of(result.records[1]).empty());
            CHECK(dar_of(result.records[2]) == 4);
        } else
            CHECK_FALSE(accepted.value());
    }
    CHECK_FALSE(collector.accept(split.value().back()));
    CHECK_FALSE(apdu::GetBlockTransfer::split(source, 0, {}));
    // 表头变更不能被当成同一个表继续合并，拒绝后仍允许正确的同号块。
    apdu::GetBlockTransfer headers(2, true, {});
    REQUIRE(headers.accept(split.value()[0]));
    auto changed = split.value()[1];
    std::get<std::vector<apdu::RecordResult>>(changed.result)[0].columns[0] =
        Oad{oi::event_sequence, 2, 0};
    CHECK(headers.accept(changed).error().code == ErrorCode::invalid_value);
    REQUIRE(headers.accept(split.value()[1]));
    CHECK_FALSE(headers.accept(split.value()[1]));
    Limits limits;
    limits.max_elements = 9;
    apdu::GetBlockTransfer bounded(2, true, limits);
    REQUIRE(bounded.accept(split.value()[0]));
    CHECK(bounded.accept(split.value()[1]).error().code == ErrorCode::resource_limit);
    limits = {};
    limits.max_data_bytes = apdu::encode_get(split.value()[0]).value().size();
    apdu::GetBlockTransfer bytes(2, true, limits);
    REQUIRE(bytes.accept(split.value()[0]));
    CHECK(bytes.accept(split.value()[1]).error().code == ErrorCode::resource_limit);
    auto wrong = split.value()[0];
    wrong.piid_acd = 3;
    apdu::GetBlockTransfer identity(2, true, {});
    CHECK_FALSE(identity.accept(wrong));
    wrong = split.value()[0];
    wrong.result = std::vector<apdu::AttributeResult>{{{oi::frequency, 2, 0}, model::UInt16{5000}}};
    CHECK_FALSE(identity.accept(wrong));
    // 重复 OAD 的两个查询保持对象边界，显式禁用合并后仍有两个结果。
    apdu::GetRecordResponse duplicate{2, true, {source.records[1], source.records[1]}, {}};
    split = apdu::GetBlockTransfer::split(duplicate, 24, {});
    REQUIRE(split);
    apdu::GetBlockTransfer separate(2, true, {}, false);
    for (const auto& block : split.value()) {
        auto accepted = separate.accept(block);
        REQUIRE(accepted);
        if (block.last)
            CHECK(std::get<apdu::GetRecordResponse>(*accepted.value()).records.size() == 2);
    }
}

TEST_CASE("探测超时逐项保留事务错误且无能力时不发包", "[standard][probe]") {
    auto executor = std::make_shared<ManualExecutor>();
    auto channels = transport::MemoryChannel::pair(executor);
    session::SessionOptions options;
    options.preset_association = true;
    options.request_timeout = std::chrono::milliseconds{10};
    auto client = std::make_shared<session::Session>(channels.first, executor, options);
    client->start();
    const std::vector<Oad> points{{oi::frequency, 2, 0}, {oi::voltage, 2, 1}};
    std::optional<Result<std::vector<service::PointResult>>> result;
    unsigned callbacks = 0;
    service::async_probe_points(client, {}, points, {}, [&](auto response) {
        ++callbacks;
        result = std::move(response);
    });
    executor->run_ready();
    CHECK_FALSE(result);
    executor->advance(std::chrono::milliseconds{10});
    REQUIRE(result);
    REQUIRE(*result);
    REQUIRE(result->value().size() == 2);
    CHECK(std::get<Error>(result->value()[0].outcome).code == ErrorCode::timeout);
    CHECK(std::get<Error>(result->value()[1].outcome).code == ErrorCode::closed);
    CHECK(callbacks == 1);
    executor->advance(std::chrono::milliseconds{100});
    CHECK(callbacks == 1);
    result.reset();
    service::async_probe_points(client, support(0x80), points, {},
                                [&](auto response) { result = std::move(response); });
    REQUIRE(result);
    CHECK(result->error().code == ErrorCode::unsupported_service);
    client->close();
    channels.second->close();
    executor->run_ready();
}

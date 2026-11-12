#include <dlt698/service/memory_records.hpp>
#include <dlt698/service/point_probe.hpp>
#include <dlt698/service/service.hpp>
#include <dlt698/transport/memory.hpp>
#include <iostream>

int main() {
    using namespace dlt698;
    namespace oi = standard::oi;
    namespace apdu = protocol::apdu;
    auto executor = std::make_shared<ManualExecutor>();
    auto channels = transport::MemoryChannel::pair(executor);
    session::SessionOptions client_options, server_options;
    server_options.role = session::Role::server;
    // 应用双方明确声明电能业务；库不因注册对象自动承诺完整计量/事件业务。
    client_options.parameters.function[0] = server_options.parameters.function[0] = 0x80;
    client_options.parameters.apdu_bytes = server_options.parameters.apdu_bytes = 80;
    auto master = std::make_shared<session::Session>(channels.first, executor, client_options);
    auto terminal = std::make_shared<session::Session>(channels.second, executor, server_options);
    auto registry = std::make_shared<service::ObjectRegistry>();
    const std::vector<model::Oad> columns{
        {oi::freeze_sequence, 2, 0}, {oi::freeze_time, 2, 0}, {oi::forward_active_energy, 2, 1}};
    auto memory = service::MemoryRecords::create(oi::daily_freeze, columns);
    if (!memory) return 1;
    std::vector<apdu::RecordRow> rows;
    for (std::uint8_t day = 1; day <= 12; ++day) {
        const model::DateTimeS time{{0x07, 0xea, 10, day, 0, 0, 0}};
        rows.push_back({model::UInt32{day}, time, model::UInt32{100000u + day}});
    }
    if (!memory.value()->replace_rows(std::move(rows)) ||
        !service::register_standard_object(*registry, oi::daily_freeze, {2}, memory.value()))
        return 2;
    auto frequency = std::make_shared<service::MemoryObject>();
    frequency->set(2, model::UInt16{5000});
    if (!service::register_standard_object(*registry, oi::frequency, {2}, frequency)) return 3;
    service::ServerService server(terminal, registry);
    service::ClientService client(master);
    master->start();
    terminal->start();
    std::optional<Result<apdu::ConnectResponse>> connected;
    master->async_connect([&](auto result) { connected = std::move(result); });
    executor->run_ready();
    if (!connected || !*connected) return 4;
    auto capabilities = standard::capabilities_from_connect(connected->value());
    if (!capabilities || !standard::require_record_service(capabilities.value())) return 5;
    // 序号区间 [1,13)，十二行超过协商的 80 字节 APDU，Client 自动收齐 GET Next。
    auto query = standard::record_sequences(
        oi::daily_freeze, 1, 13, {{oi::freeze_time, 2, 0}, {oi::forward_active_energy, 2, 1}});
    if (!query) return 6;
    std::optional<Result<apdu::RecordResult>> record;
    client.async_get_record(query.value(), [&](auto result) { record = std::move(result); });
    executor->run_ready();
    if (!record || !*record) {
        if (record) std::cerr << record->error().context << '\n';
        return 7;
    }
    const auto data = std::get_if<std::vector<apdu::RecordRow>>(&record->value().result);
    if (!data || data->size() != 12) return 8;
    std::cout << "daily freeze: " << data->size() << " rows after GET Next\n";
    for (const auto& row : *data) {
        auto number = standard::engineering_values({oi::forward_active_energy, 2, 1}, row[1]);
        if (!number) return 9;
        std::cout << "2026-10-" << unsigned(row[0].as<model::DateTimeS>().value[3]) << " "
                  << standard::decimal_text(number.value()[0]) << " kWh\n";
    }
    // 显式探测普通点位，设备逐项 DAR=4 与正常频率值分别保留。
    std::optional<Result<std::vector<service::PointResult>>> probed;
    service::async_probe_points(master, capabilities.value(),
                                {{oi::frequency, 2, 0}, {oi::voltage, 2, 1}}, {},
                                [&](auto result) { probed = std::move(result); });
    executor->run_ready();
    if (!probed || !*probed || probed->value().size() != 2) return 10;
    if (!std::holds_alternative<model::Data>(probed->value()[0].outcome) ||
        !std::holds_alternative<std::uint8_t>(probed->value()[1].outcome))
        return 11;
    std::cout << "probe: frequency=Data, voltage=DAR "
              << unsigned(std::get<std::uint8_t>(probed->value()[1].outcome)) << '\n';
    std::optional<Result<void>> released;
    master->async_release([&](auto result) { released = std::move(result); });
    executor->run_ready();
    master->close();
    terminal->close();
    executor->run_ready();
    return released && *released ? 0 : 12;
}

#include <dlt698/service/service.hpp>
#include <dlt698/service/standard_object.hpp>
#include <dlt698/transport/memory.hpp>
#include <iomanip>
#include <iostream>

int main() {
    using namespace dlt698;
    namespace oi = standard::oi;
    const standard::DeviceLayout layout{standard::Wiring::three_phase, 1, 3};
    auto executor = std::make_shared<ManualExecutor>();
    auto channels = transport::MemoryChannel::pair(executor);
    session::SessionOptions options;
    options.role = session::Role::server;
    auto master = std::make_shared<session::Session>(channels.first, executor);
    auto terminal = std::make_shared<session::Session>(channels.second, executor, options);
    auto registry = std::make_shared<service::ObjectRegistry>();
    auto bind = [&](std::uint16_t identifier, std::uint8_t attribute, model::Data value) {
        auto provider = std::make_shared<service::MemoryObject>();
        provider->set(attribute, std::move(value));
        return service::register_standard_object(*registry, identifier, {attribute}, provider,
                                                 layout);
    };
    // 最大需量每个费率元素同时携带有符号数值及发生时间，不能拆成普通整数数组。
    const model::DateTimeS occurred{{0x07, 0xea, 10, 5, 12, 30, 0}};
    const model::Data demand = model::Structure{{model::Int32{-123456}, occurred}};
    model::Array status;
    status.value.assign(7, model::BitString{16, {0x80, 0x04}});
    if (!bind(oi::forward_active_energy_a, 2,
              model::Array{{model::UInt32{123456}, model::UInt32{123456}}}) ||
        !bind(oi::combination_reactive_1_maximum_demand_a, 2, model::Array{{demand, demand}}) ||
        !bind(oi::voltage_harmonics, 3,
              model::Array{{model::Int16{345}, model::Int16{125}, model::Int16{-5}}}) ||
        !bind(oi::operating_status, 2, std::move(status)) ||
        !bind(oi::meter_number, 2, model::OctetString{{0x12, 0x34}}) ||
        !bind(oi::maximum_demand_period, 2, model::UInt8{15}))
        return 1;
    service::ServerService server(terminal, registry);
    service::ClientService client(master);
    master->start();
    terminal->start();
    std::optional<Result<protocol::apdu::ConnectResponse>> connected;
    master->async_connect([&](auto result) { connected = std::move(result); });
    executor->run_ready();
    if (!connected || !*connected || connected->value().result) return 2;
    auto energy_point =
        standard::energy_oad(oi::forward_active_energy, standard::Phase::a, 0, layout);
    auto demand_point = standard::demand_oad(oi::combination_reactive_1_maximum_demand,
                                             standard::Phase::a, 0, layout);
    auto harmonic_point =
        standard::harmonic_oad(oi::voltage_harmonics, standard::Phase::b, 2, layout);
    if (!energy_point || !demand_point || !harmonic_point) return 3;
    std::optional<Result<protocol::apdu::GetResponse>> response;
    client.async_get_list({energy_point.value(),
                           demand_point.value(),
                           harmonic_point.value(),
                           {oi::operating_status, 2, 1},
                           {oi::meter_number, 2, 0},
                           {oi::maximum_demand_period, 2, 0}},
                          [&](auto result) { response = std::move(result); });
    executor->run_ready();
    if (!response || !*response) return 4;
    for (const auto& entry : response->value().attributes) {
        const auto object = standard::find_object(entry.attribute.oi);
        std::cout << object->name << ": ";
        if (const auto dar = std::get_if<std::uint8_t>(&entry.result)) {
            std::cout << "DAR=" << unsigned(*dar) << '\n';
            return 5;
        }
        const auto& data = std::get<model::Data>(entry.result);
        if (object->class_id == 2) {
            const auto readings = standard::demand_values(entry.attribute, data, layout);
            if (!readings) return 6;
            const auto& reading = readings.value()[0];
            const auto& time = reading.occurred_at.value;
            // 原始时间无附加时区；模拟数据包含完整字段，直接展示标准中的发生时间。
            std::cout << standard::decimal_text(reading.number) << ' '
                      << standard::unit_symbol(reading.number.scaling.unit) << " @ "
                      << std::setfill('0') << std::setw(4) << ((unsigned(time[0]) << 8) | time[1])
                      << '-' << std::setw(2) << unsigned(time[2]) << '-' << std::setw(2)
                      << unsigned(time[3]) << ' ' << std::setw(2) << unsigned(time[4]) << ':'
                      << std::setw(2) << unsigned(time[5]) << ':' << std::setw(2)
                      << unsigned(time[6]);
        } else {
            auto numbers = standard::engineering_values(entry.attribute, data, layout);
            if (numbers) {
                for (const auto& number : numbers.value())
                    std::cout << standard::decimal_text(number) << ' '
                              << standard::unit_symbol(number.scaling.unit);
            } else if (numbers.error().code == ErrorCode::unsupported_tag) {
                auto bytes = codec::encode_data(data);
                if (!bytes) return 7;
                std::cout << to_hex(bytes.value());
            } else
                return 8;
        }
        std::cout << '\n';
    }
    std::optional<Result<void>> released;
    master->async_release([&](auto result) { released = std::move(result); });
    executor->run_ready();
    master->close();
    terminal->close();
    executor->run_ready();
    return released && *released ? 0 : 9;
}

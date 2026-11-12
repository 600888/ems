#include <dlt698/service/service.hpp>
#include <dlt698/service/standard_object.hpp>
#include <dlt698/transport/memory.hpp>
#include <iostream>

int main() {
    using namespace dlt698;
    const standard::DeviceLayout layout{standard::Wiring::three_phase, 4};
    auto executor = std::make_shared<ManualExecutor>();
    auto channels = transport::MemoryChannel::pair(executor);
    session::SessionOptions server_options;
    server_options.role = session::Role::server;
    auto master = std::make_shared<session::Session>(channels.first, executor);
    auto terminal = std::make_shared<session::Session>(channels.second, executor, server_options);
    auto registry = std::make_shared<service::ObjectRegistry>();

    // 数值均为协议原始整数，目录负责精确类型及设备布局校验，不产生硬件数据。
    auto voltage = std::make_shared<service::MemoryObject>();
    voltage->set(2, model::Array{{model::UInt16{2413}, model::UInt16{2414}, model::UInt16{2415}}});
    auto current = std::make_shared<service::MemoryObject>();
    current->set(2, model::Array{{model::Int32{1000}, model::Int32{2000}, model::Int32{3000}}});
    auto energy = std::make_shared<service::MemoryObject>();
    energy->set(2, model::Array{{model::UInt32{123456}, model::UInt32{10000}, model::UInt32{20000},
                                 model::UInt32{30000}, model::UInt32{63456}}});
    auto address = std::make_shared<service::MemoryObject>();
    address->set(2, model::OctetString{{0x12, 0x34, 0x56, 0x78, 0x90, 0x12}});
    if (!service::register_standard_object(*registry, standard::oi::voltage, {2}, voltage,
                                           layout) ||
        !service::register_standard_object(*registry, standard::oi::current, {2}, current,
                                           layout) ||
        !service::register_standard_object(*registry, standard::oi::forward_active_energy, {2},
                                           energy, layout) ||
        !service::register_standard_object(*registry, standard::oi::communication_address, {2},
                                           address, layout))
        return 1;
    service::ServerService server(terminal, registry);
    service::ClientService client(master);
    master->start();
    terminal->start();
    std::optional<Result<protocol::apdu::ConnectResponse>> association;
    master->async_connect([&](auto r) { association = std::move(r); });
    executor->run_ready();
    if (!association || !*association || association->value().result) return 2;

    auto a_voltage = standard::phase_oad(standard::oi::voltage, standard::Phase::a, layout);
    auto a_current = standard::phase_oad(standard::oi::current, standard::Phase::a, layout);
    auto total_energy = standard::tariff_oad(standard::oi::forward_active_energy, 0, layout);
    if (!a_voltage || !a_current || !total_energy) return 3;
    std::optional<Result<protocol::apdu::GetResponse>> response;
    client.async_get_list({a_voltage.value(),
                           a_current.value(),
                           total_energy.value(),
                           {standard::oi::communication_address, 2, 0},
                           {standard::oi::frequency, 2, 0}},
                          [&](auto r) { response = std::move(r); });
    executor->run_ready();
    if (!response || !*response) return 4;
    for (const auto& entry : response->value().attributes) {
        const auto object = standard::find_object(entry.attribute.oi);
        std::cout << object->name << ": ";
        // 外层事务成功后仍逐项检查 DAR；目录存在的频率在本设备没有 provider。
        if (const auto dar = std::get_if<std::uint8_t>(&entry.result)) {
            std::cout << "DAR=" << unsigned(*dar) << '\n';
            continue;
        }
        const auto& data = std::get<model::Data>(entry.result);
        const auto definition = standard::find_attribute(entry.attribute);
        if (definition->scaling) {
            auto numbers = standard::engineering_values(entry.attribute, data, layout);
            if (!numbers) return 5;
            for (const auto& number : numbers.value())
                std::cout << standard::decimal_text(number) << ' '
                          << standard::unit_symbol(number.scaling.unit);
        } else {
            auto wire = codec::encode_data(data);
            if (!wire) return 6;
            std::cout << to_hex(wire.value());
        }
        std::cout << '\n';
    }
    std::optional<Result<void>> released;
    master->async_release([&](auto r) { released = std::move(r); });
    executor->run_ready();
    master->close();
    terminal->close();
    executor->run_ready();
    return released && *released ? 0 : 7;
}

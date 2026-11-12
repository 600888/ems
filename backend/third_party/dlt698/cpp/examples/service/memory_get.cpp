#include <dlt698/service/service.hpp>
#include <dlt698/transport/memory.hpp>
#include <iostream>

int main() {
    using namespace dlt698;
    auto executor = std::make_shared<ManualExecutor>();
    auto channels = transport::MemoryChannel::pair(executor, {3, 65536});
    session::SessionOptions terminal_options;
    terminal_options.role = session::Role::server;
    auto master = std::make_shared<session::Session>(channels.first, executor);
    auto terminal = std::make_shared<session::Session>(channels.second, executor, terminal_options);
    auto objects = std::make_shared<service::ObjectRegistry>();
    auto voltage = std::make_shared<service::MemoryObject>();
    // 模拟对象保存协议原始值，此处不隐含单位或倍率，也不代表真实计量能力。
    voltage->set(2, model::Array{{model::UInt16{2413}, model::UInt16{2414}, model::UInt16{2415}}});
    auto registered = objects->register_object(
        {0x2000, "三相电压原始值", {{2, model::DataType::array, true}}}, voltage);
    if (!registered) return 1;
    service::ServerService server(terminal, objects);
    service::ClientService client(master);
    master->start();
    terminal->start();
    std::optional<Result<protocol::apdu::ConnectResponse>> association;
    master->async_connect([&](auto result) { association = std::move(result); });
    executor->run_ready();
    if (!association || !*association || association->value().result) return 2;
    std::optional<Result<protocol::apdu::GetResponse>> response;
    client.async_get_list({{0x2000, 2, 0}, {0x9999, 2, 0}},
                          [&](auto result) { response = std::move(result); });
    executor->run_ready();
    if (!response || !*response) return 3;
    for (const auto& attribute : response->value().attributes) {
        std::cout << "OI=" << std::hex << attribute.attribute.oi << std::dec;
        if (const auto dar = std::get_if<std::uint8_t>(&attribute.result))
            std::cout << " DAR=" << unsigned(*dar) << '\n';
        else {
            auto bytes = codec::encode_data(std::get<model::Data>(attribute.result));
            if (!bytes) return 4;
            std::cout << " Data=" << to_hex(bytes.value()) << '\n';
        }
    }
    std::optional<Result<void>> released;
    master->async_release([&](auto result) { released = std::move(result); });
    executor->run_ready();
    master->close();
    terminal->close();
    executor->run_ready();
    return released && *released ? 0 : 5;
}

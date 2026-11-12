#include <dlt698/service/sync.hpp>
#include <dlt698/transport/memory.hpp>
#include <dlt698/transport/serial_link.hpp>
#include <iostream>

int main() {
    using namespace dlt698;
    auto executor = std::make_shared<ManualExecutor>();
    auto raw = transport::MemoryChannel::pair(executor);
    // 在内存中同样经过 FE 和串行间隔适配，真实设备可把 raw 替换为 SerialChannel。
    auto master_link = transport::SerialLinkChannel::wrap(raw.first, executor);
    auto terminal_link = transport::SerialLinkChannel::wrap(raw.second, executor);
    session::SessionOptions options;
    options.role = session::Role::server;
    auto master = std::make_shared<session::Session>(master_link, executor);
    auto terminal = std::make_shared<session::Session>(terminal_link, executor, options);
    auto objects = std::make_shared<service::ObjectRegistry>();
    auto object = std::make_shared<service::MemoryObject>();
    object->set(2, model::UInt16{10});
    object->bind_method(1,
                        [weak = std::weak_ptr<service::MemoryObject>(object)](
                            const auto&, const model::Data& parameter) -> service::ActionValue {
                            weak.lock()->set(2, parameter);
                            return {0, parameter};
                        });
    if (!objects->register_object({0x2000,
                                   "可写值及模拟方法",
                                   {{2, model::DataType::uint16, true, true}},
                                   {{1, model::DataType::uint16, model::DataType::uint16, true}}},
                                  object))
        return 1;
    service::ServerService server(terminal, objects);
    service::SyncClientService client(master,
                                      [executor](auto elapsed) { executor->advance(elapsed); });
    master->start();
    terminal->start();
    auto connect = client.connect();
    if (!connect || connect.value().result) return 2;
    auto set = client.set({0x2000, 2, 0}, model::UInt16{25});
    if (!set || set.value()) return 3;
    auto action = client.action({0x2000, 1, 0}, model::UInt16{42});
    if (!action || action.value().dar) return 4;
    auto value = client.get({0x2000, 2, 0});
    if (!value || !std::holds_alternative<model::Data>(value.value())) return 5;
    std::cout << "SET DAR=" << unsigned(set.value())
              << " ACTION DAR=" << unsigned(action.value().dar)
              << " GET UInt16=" << std::get<model::Data>(value.value()).as<model::UInt16>().value
              << '\n';
    auto released = client.release();
    master->close();
    terminal->close();
    executor->run_ready();
    return released ? 0 : 6;
}

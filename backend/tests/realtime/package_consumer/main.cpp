#include <realtime/realtime_client.h>

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto ctx = ems::core::make_call_context(std::chrono::seconds(3));
    auto opened = ems::realtime::RealtimeClient::open(argv[1], ctx);
    if (!ems::core::is_ok(opened)) return 3;
    auto client = std::get<0>(std::move(opened));
    const ems::realtime::RecordKey key{{ems::realtime::ScopeKind::Unit, "package01"},
                                       ems::realtime::RecordKind::Point,
                                       {"pcs01"},
                                       "power"};
    auto current = client->read(key, ctx);
    if (!ems::core::is_ok(current)) {
        auto write = client->compareExchange({key, {{"value", "42"}}, {}}, ctx);
        if (!ems::core::is_ok(write)) return 4;
    }
    auto read = client->read(key, ctx);
    if (!ems::core::is_ok(read) || ems::core::value_of(read).fields.at("value") != "42") return 5;
    std::cout << "shared-library consumer: 42\n";
}

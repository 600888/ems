#include <rtdb/client/realtime_client.h>
#include <rtdb/client/rtdb.h>

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto ctx = ems::core::make_call_context(std::chrono::seconds(3));
    auto opened = ems::rtdb::RealtimeClient::open(argv[1], ctx);
    if (!ems::core::is_ok(opened)) return 3;
    auto client = std::get<0>(std::move(opened));
    const ems::rtdb::RecordKey key{{ems::rtdb::ScopeKind::Unit, "package01"},
                                   ems::rtdb::RecordKind::Point,
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
    ems::rtdb::RtDbConfig config;
    config.config_path = argv[1];
    auto business = ems::rtdb::createRtDb(config);
    if (!business) return 6;
    ems::rtdb::YcPoint point;
    point.value_ = 84;
    if (!business->setYc("package.power", point)) return 7;
    if (!business->getYc("package.power", point) || point.value_ != 84) return 8;
    std::cout << "business-library consumer: 84\n";
}

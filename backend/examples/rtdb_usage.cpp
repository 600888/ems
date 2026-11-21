#include <rtdb/client/point_client.h>

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto ctx = ems::core::make_call_context(std::chrono::seconds(3));
    auto catalog = std::make_shared<ems::rtdb::RuntimeCatalog>(
        ems::rtdb::Scope{ems::rtdb::ScopeKind::Station, "station"}, "1", "", 60000);
    ems::rtdb::RuntimePoint point;
    point.point_id = ems::core::PointId{"power"};
    point.address = {{ems::rtdb::ScopeKind::Station, "station"}, {"pcs"}, "power"};
    if (!catalog->addPoint(point)) return 3;
    auto opened = ems::rtdb::PointClient::open(argv[1], catalog, ctx);
    if (!ems::core::is_ok(opened)) return 4;
    auto client = ems::core::value_of(opened);
    if (!ems::core::is_ok(client->installWriter(ctx))) return 5;
    if (!ems::core::is_ok(client->update("power", 42, ems::model::Quality::Good, 1234, ctx)))
        return 6;
    auto read = client->readPointJson("yc", "power", ctx);
    if (!ems::core::is_ok(read)) return 7;
    std::cout << ems::core::value_of(read) << '\n';
}

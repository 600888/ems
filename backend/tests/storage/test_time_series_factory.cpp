#include <cassert>

#include "native_adapters.h"

int main() {
    const auto taos = ems::storage::parse_time_series_backend("tdengine");
    assert(ems::core::is_ok(taos));
    assert(ems::core::value_of(taos) == ems::storage::TimeSeriesBackend::Tdengine);

    const auto influx = ems::storage::parse_time_series_backend("influxdb");
    assert(ems::core::is_ok(influx));
    ems::storage::TimeSeriesConnectionOptions options;
    options.backend = ems::core::value_of(influx);
    const auto unavailable = ems::storage::connect_time_series(options);
    assert(!ems::core::is_ok(unavailable));
    assert(ems::core::error_of(unavailable).code == ems::core::ErrorCode::Unsupported);

    const auto unknown = ems::storage::parse_time_series_backend("unknown");
    assert(!ems::core::is_ok(unknown));
    assert(ems::core::error_of(unknown).code == ems::core::ErrorCode::InvalidArgument);
    return 0;
}

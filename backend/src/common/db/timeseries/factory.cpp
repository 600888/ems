#include <common/database/timeseries/factory.h>

#include <memory>
#include <string>

namespace ems::database {

#ifndef EMS_DATABASE_HAVE_TAOS
core::Result<std::unique_ptr<ITimeSeriesDatabase>> connect_taos_series(
    const TaosConnectionOptions&) {
    return core::make_error(core::ErrorCode::Unsupported, core::WriteEffect::NotApplicable,
                            "TDengine backend is not built");
}
#endif

core::Result<TimeSeriesBackend> parse_time_series_backend(const std::string& name) {
    if (name == "taos" || name == "tdengine") return TimeSeriesBackend::Tdengine;
    if (name == "influxdb") return TimeSeriesBackend::InfluxDb;
    return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplicable,
                            "unknown time-series backend");
}

core::Result<std::unique_ptr<ITimeSeriesDatabase>> connect_time_series(
    const TimeSeriesConnectionOptions& options) {
    switch (options.backend) {
        case TimeSeriesBackend::Tdengine:
            return connect_taos_series(options.taos);
        case TimeSeriesBackend::InfluxDb:
            return core::make_error(core::ErrorCode::Unsupported, core::WriteEffect::NotApplicable,
                                    "InfluxDB backend is not implemented");
    }
    return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplicable,
                            "unknown time-series backend");
}

}  // namespace ems::database

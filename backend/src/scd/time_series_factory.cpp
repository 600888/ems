#include <memory>
#include <string>

#include "native_adapters.h"

namespace ems::storage {

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
#ifdef EMS_STORAGE_HAVE_TAOS
            return connect_taos_series(options.taos);
#else
            return core::make_error(core::ErrorCode::Unsupported, core::WriteEffect::NotApplicable,
                                    "TDengine backend is not built");
#endif
        case TimeSeriesBackend::InfluxDb:
            return core::make_error(core::ErrorCode::Unsupported, core::WriteEffect::NotApplicable,
                                    "InfluxDB backend is not implemented");
    }
    return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplicable,
                            "unknown time-series backend");
}

}  // namespace ems::storage

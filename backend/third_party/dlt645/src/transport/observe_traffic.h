#ifndef DLT645_TRANSPORT_OBSERVE_TRAFFIC_H
#define DLT645_TRANSPORT_OBSERVE_TRAFFIC_H

#include "dlt645/transport/traffic.h"

namespace dlt645::transport {

    template <typename Handler, typename... Args>
    void observeTraffic(const Handler& handler,
                        TrafficDirection direction,
                        TrafficBytes bytes,
                        std::error_code error,
                        Args... args) noexcept
    {
        if (handler) {
            try {
                handler(args..., TrafficEvent { direction, std::chrono::system_clock::now(), bytes, TrafficResult(error) });
            } catch (...) {
                // A traffic observer cannot interrupt I/O.
            }
        }
    }

} // namespace dlt645::transport

#endif

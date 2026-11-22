#ifndef DLT645_TRANSPORT_TRAFFIC_H
#define DLT645_TRANSPORT_TRAFFIC_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <system_error>

namespace dlt645::transport {

    enum class TrafficDirection { receive, send };

    class TrafficBytes {
    public:
        TrafficBytes(const uint8_t* data, std::size_t size) noexcept : data_(data), size_(size) {}

        const uint8_t* data() const noexcept { return data_; }
        std::size_t size() const noexcept { return size_; }
        bool empty() const noexcept { return size_ == 0; }
        const uint8_t* begin() const noexcept { return data_; }
        const uint8_t* end() const noexcept { return size_ == 0 ? data_ : data_ + size_; }

    private:
        const uint8_t* data_;
        std::size_t size_;
    };

    class TrafficResult {
    public:
        explicit TrafficResult(std::error_code error = {}) noexcept : error_(error) {}
        explicit operator bool() const noexcept { return !error_; }
        const std::error_code& error() const noexcept { return error_; }

    private:
        std::error_code error_;
    };

    // RX is a successful read before decoding. TX contains all submitted bytes even on a failed write.
    // The event and its byte view are borrowed for the duration of the callback.
    struct TrafficEvent {
        TrafficDirection direction;
        std::chrono::system_clock::time_point timestamp;
        TrafficBytes bytes;
        TrafficResult result;
    };

    // Runs on the transport's I/O thread. Keep it short and avoid synchronous calls into the transport.
    // Exceptions are isolated from the protocol.
    using TrafficHandler = std::function<void(const TrafficEvent&)>;
    using ServerTrafficHandler = std::function<void(uint64_t, const TrafficEvent&)>;

} // namespace dlt645::transport

#endif

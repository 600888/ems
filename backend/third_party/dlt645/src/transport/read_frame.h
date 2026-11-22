#ifndef DLT645_TRANSPORT_READ_FRAME_H
#define DLT645_TRANSPORT_READ_FRAME_H

#include <array>
#include <chrono>
#include <future>
#include <memory>
#include <vector>
#include <asio.hpp>
#include "dlt645/protocol/protocol.h"
#include "transport/observe_traffic.h"

namespace dlt645::transport {

    // Keeps reading until a complete frame arrives or the request times out.
    template <typename Stream>
    class FrameReadOperation : public std::enable_shared_from_this<FrameReadOperation<Stream>> {
    public:
        FrameReadOperation(std::shared_ptr<Stream> stream,
                           protocol::FrameStreamDecoder& decoder,
                           asio::io_context& context,
                           std::chrono::milliseconds timeout,
                           std::shared_ptr<std::promise<std::vector<uint8_t>>> promise,
                           std::shared_ptr<const TrafficHandler> traffic)
            : stream_(std::move(stream))
            , decoder_(decoder)
            , timer_(context)
            , timeout_(timeout)
            , promise_(std::move(promise))
            , traffic_(std::move(traffic))
        {
        }

        void start()
        {
            if (auto frame = decoder_.nextFrame()) {
                finish(frame->serialize());
                return;
            }
            timer_.expires_after(timeout_);
            auto self = this->shared_from_this();
            timer_.async_wait([self](const std::error_code& error) {
                if (!error && !self->done_) {
                    self->decoder_.clear();
                    std::error_code ignored;
                    self->stream_->cancel(ignored);
                    self->finish({});
                }
            });
            readMore();
        }

    private:
        void readMore()
        {
            auto self = this->shared_from_this();
            stream_->async_read_some(asio::buffer(bytes_), [self](const std::error_code& error, size_t size) {
                if (!error && size != 0 && self->traffic_) {
                    observeTraffic(*self->traffic_, TrafficDirection::receive,
                                   { self->bytes_.data(), size }, {});
                }
                if (self->done_)
                    return;
                if (error) {
                    self->decoder_.clear();
                    self->finish({});
                    return;
                }
                self->decoder_.append(self->bytes_.data(), size);
                if (auto frame = self->decoder_.nextFrame()) {
                    self->finish(frame->serialize());
                } else {
                    self->readMore();
                }
            });
        }

        void finish(std::vector<uint8_t> result)
        {
            if (done_)
                return;
            done_ = true;
            timer_.cancel();
            promise_->set_value(std::move(result));
        }

        std::shared_ptr<Stream> stream_;
        protocol::FrameStreamDecoder& decoder_;
        asio::steady_timer timer_;
        std::chrono::milliseconds timeout_;
        std::shared_ptr<std::promise<std::vector<uint8_t>>> promise_;
        std::shared_ptr<const TrafficHandler> traffic_;
        std::array<uint8_t, 1024> bytes_ {};
        bool done_ = false;
    };

} // namespace dlt645::transport

#endif

#include <common/messaging/zmq_command_channel.h>
#include <zmq.h>

#if ZMQ_VERSION_MAJOR < 4 || (ZMQ_VERSION_MAJOR == 4 && ZMQ_VERSION_MINOR < 3)
#error "ems_zmq_messaging requires libzmq 4.3 or newer"
#endif

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <future>
#include <limits>
#include <string_view>
#include <thread>
#include <utility>

namespace ems::messaging {
namespace {

constexpr std::string_view kRequestPrefix = "EMS-CMD/1";
constexpr std::string_view kReplyPrefix = "EMS-ACK/1";

bool valid_id(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    for (unsigned char ch : id) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
              ch == '-' || ch == '_'))
            return false;
    }
    return true;
}

bool valid_config(const ZmqCommandConfig& config) {
    return !config.endpoint.empty() && config.max_message_bytes >= 512 &&
           config.max_message_bytes <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
           config.request_timeout_ms > 0 && config.reply_timeout_ms > 0 &&
           config.receive_poll_ms > 0 && config.high_water_mark > 0;
}

void close_command_socket(void* socket, void* context) {
    if (socket) zmq_close(socket);
    if (context) zmq_ctx_term(context);
}

bool set_int_option(void* socket, int option, int value) {
    return zmq_setsockopt(socket, option, &value, sizeof(value)) == 0;
}

bool has_more_parts(void* socket, bool& more) {
    int value = 0;
    std::size_t size = sizeof(value);
    if (zmq_getsockopt(socket, ZMQ_RCVMORE, &value, &size) != 0) return false;
    more = value != 0;
    return true;
}

bool receive_frame(void* socket, std::size_t max_bytes, std::string& frame, int* error = nullptr) {
    zmq_msg_t message;
    if (zmq_msg_init(&message) != 0) {
        if (error) *error = zmq_errno();
        return false;
    }
    const int received = zmq_msg_recv(&message, socket, 0);
    bool ok = received >= 0 && zmq_msg_size(&message) <= max_bytes;
    if (error) *error = received < 0 ? zmq_errno() : (ok ? 0 : EMSGSIZE);
    if (ok) {
        const auto* bytes = static_cast<const char*>(zmq_msg_data(&message));
        frame.assign(bytes, bytes + zmq_msg_size(&message));
    }
    zmq_msg_close(&message);
    return ok;
}

std::string encode_request(const ZmqCommandRequest& request) {
    std::string frame(kRequestPrefix);
    frame.push_back('\0');
    frame += request.command_id;
    frame.push_back('\0');
    frame += request.payload_json;
    return frame;
}

bool decode_request(const std::string& frame, ZmqCommandRequest& request) {
    if (frame.size() <= kRequestPrefix.size() + 2 ||
        frame.compare(0, kRequestPrefix.size(), kRequestPrefix.data(), kRequestPrefix.size()) !=
            0 ||
        frame[kRequestPrefix.size()] != '\0')
        return false;
    const auto id_start = kRequestPrefix.size() + 1;
    const auto split = frame.find('\0', id_start);
    if (split == std::string::npos) return false;
    request.command_id = frame.substr(id_start, split - id_start);
    request.payload_json = frame.substr(split + 1);
    return valid_id(request.command_id) && !request.payload_json.empty();
}

char reply_code(ZmqCommandReplyCode code) {
    switch (code) {
        case ZmqCommandReplyCode::Accepted:
            return 'A';
        case ZmqCommandReplyCode::Duplicate:
            return 'D';
        case ZmqCommandReplyCode::Rejected:
            return 'R';
        case ZmqCommandReplyCode::RetryLater:
            return 'T';
        case ZmqCommandReplyCode::Invalid:
            return 'I';
        case ZmqCommandReplyCode::Unknown:
            return 'U';
    }
    return 'U';
}

ZmqCommandReplyCode parse_reply_code(char code) {
    switch (code) {
        case 'A':
            return ZmqCommandReplyCode::Accepted;
        case 'D':
            return ZmqCommandReplyCode::Duplicate;
        case 'R':
            return ZmqCommandReplyCode::Rejected;
        case 'T':
            return ZmqCommandReplyCode::RetryLater;
        case 'I':
            return ZmqCommandReplyCode::Invalid;
        case 'U':
            return ZmqCommandReplyCode::Unknown;
        default:
            return ZmqCommandReplyCode::Unknown;
    }
}

std::string encode_reply(const std::string& command_id, const ZmqCommandReply& reply,
                         std::size_t max_bytes) {
    std::string frame(kReplyPrefix);
    frame.push_back('\0');
    frame.push_back(reply_code(reply.code));
    frame += command_id;
    frame.push_back('\0');
    if (frame.size() < max_bytes) {
        frame.append(reply.reason, 0, std::min(reply.reason.size(), max_bytes - frame.size()));
    }
    return frame;
}

bool decode_reply(const std::string& frame, const std::string& command_id, ZmqCommandReply& reply) {
    if (frame.size() < kReplyPrefix.size() + 3 ||
        frame.compare(0, kReplyPrefix.size(), kReplyPrefix.data(), kReplyPrefix.size()) != 0 ||
        frame[kReplyPrefix.size()] != '\0')
        return false;
    const auto id_start = kReplyPrefix.size() + 2;
    const auto split = frame.find('\0', id_start);
    if (split == std::string::npos || frame.substr(id_start, split - id_start) != command_id)
        return false;
    reply.code = parse_reply_code(frame[kReplyPrefix.size() + 1]);
    reply.reason = frame.substr(split + 1);
    return reply.code != ZmqCommandReplyCode::Unknown || frame[kReplyPrefix.size() + 1] == 'U';
}

}  // namespace

ZmqCommandClient::ZmqCommandClient(ZmqCommandConfig config) : config_(std::move(config)) {}

ZmqCommandReply ZmqCommandClient::send(const ZmqCommandRequest& request) const {
    if (!valid_config(config_) || !valid_id(request.command_id) || request.payload_json.empty()) {
        return {ZmqCommandReplyCode::Invalid, "invalid command request"};
    }
    const std::string frame = encode_request(request);
    if (frame.size() > config_.max_message_bytes) {
        return {ZmqCommandReplyCode::Invalid, "command frame exceeds limit"};
    }

    void* context = zmq_ctx_new();
    void* socket = context ? zmq_socket(context, ZMQ_REQ) : nullptr;
    int linger = 0;
    int immediate = 1;
    std::int64_t max_size = static_cast<std::int64_t>(config_.max_message_bytes);
    bool ok = socket && set_int_option(socket, ZMQ_LINGER, linger) &&
              set_int_option(socket, ZMQ_IMMEDIATE, immediate) &&
              set_int_option(socket, ZMQ_SNDTIMEO, config_.request_timeout_ms) &&
              set_int_option(socket, ZMQ_RCVTIMEO, config_.request_timeout_ms) &&
              set_int_option(socket, ZMQ_SNDHWM, config_.high_water_mark) &&
              set_int_option(socket, ZMQ_RCVHWM, config_.high_water_mark) &&
              zmq_setsockopt(socket, ZMQ_MAXMSGSIZE, &max_size, sizeof(max_size)) == 0 &&
              zmq_connect(socket, config_.endpoint.c_str()) == 0;
    if (!ok) {
        close_command_socket(socket, context);
        return {ZmqCommandReplyCode::Unknown, "transport initialization failed"};
    }

    std::string response;
    ok = zmq_send(socket, frame.data(), frame.size(), 0) >= 0 &&
         receive_frame(socket, config_.max_message_bytes, response);
    bool extra_parts = false;
    if (ok) ok = has_more_parts(socket, extra_parts) && !extra_parts;
    close_command_socket(socket, context);
    if (!ok) return {ZmqCommandReplyCode::Unknown, "request or reply timed out"};

    ZmqCommandReply reply;
    if (!decode_reply(response, request.command_id, reply)) {
        return {ZmqCommandReplyCode::Unknown, "invalid command reply"};
    }
    return reply;
}

struct ZmqCommandServer::Impl {
    std::atomic<bool> stopping{false};
    std::atomic<bool> active{false};
    std::thread worker;
    std::string endpoint;
};

ZmqCommandServer::ZmqCommandServer() : impl_(std::make_unique<Impl>()) {}

ZmqCommandServer::~ZmqCommandServer() { stop(); }

bool ZmqCommandServer::start(const ZmqCommandConfig& config, Handler handler) {
    if (!valid_config(config) || !handler || impl_->worker.joinable()) return false;
    impl_->stopping = false;
    std::promise<bool> initialized;
    auto future = initialized.get_future();
    impl_->worker = std::thread([state = impl_.get(), config, handler = std::move(handler),
                                 signal = std::move(initialized)]() mutable {
        void* context = zmq_ctx_new();
        void* socket = context ? zmq_socket(context, ZMQ_REP) : nullptr;
        int linger = 0;
        std::int64_t max_size = static_cast<std::int64_t>(config.max_message_bytes);
        bool ok = socket && set_int_option(socket, ZMQ_LINGER, linger) &&
                  set_int_option(socket, ZMQ_RCVTIMEO, config.receive_poll_ms) &&
                  set_int_option(socket, ZMQ_SNDTIMEO, config.reply_timeout_ms) &&
                  set_int_option(socket, ZMQ_SNDHWM, config.high_water_mark) &&
                  set_int_option(socket, ZMQ_RCVHWM, config.high_water_mark) &&
                  zmq_setsockopt(socket, ZMQ_MAXMSGSIZE, &max_size, sizeof(max_size)) == 0 &&
                  zmq_bind(socket, config.endpoint.c_str()) == 0;
        if (!ok) {
            close_command_socket(socket, context);
            signal.set_value(false);
            return;
        }
        char endpoint[512] = {};
        std::size_t endpoint_size = sizeof(endpoint);
        if (zmq_getsockopt(socket, ZMQ_LAST_ENDPOINT, endpoint, &endpoint_size) != 0 ||
            endpoint_size == 0) {
            close_command_socket(socket, context);
            signal.set_value(false);
            return;
        }
        state->endpoint.assign(endpoint, endpoint_size - (endpoint[endpoint_size - 1] == '\0'));
        state->active = true;
        signal.set_value(true);

        while (!state->stopping) {
            std::string frame;
            int error = 0;
            if (!receive_frame(socket, config.max_message_bytes, frame, &error)) {
                if (error == EAGAIN || error == EINTR) continue;
                break;
            }
            bool more = false;
            if (!has_more_parts(socket, more)) break;
            const bool multipart = more;
            int parts = 0;
            while (more && parts++ < 16) {
                std::string ignored;
                if (!receive_frame(socket, config.max_message_bytes, ignored) ||
                    !has_more_parts(socket, more)) {
                    more = true;
                    break;
                }
            }
            if (more) break;
            ZmqCommandRequest request;
            ZmqCommandReply reply{ZmqCommandReplyCode::Invalid, "invalid command frame"};
            if (!multipart && decode_request(frame, request)) {
                try {
                    reply = handler(request);
                } catch (...) {
                    reply = {ZmqCommandReplyCode::Unknown, "handler result unknown"};
                }
            }
            std::string response =
                encode_reply(request.command_id, reply, config.max_message_bytes);
            if (zmq_send(socket, response.data(), response.size(), 0) < 0) break;
        }
        state->active = false;
        close_command_socket(socket, context);
    });
    if (!future.get()) {
        impl_->worker.join();
        return false;
    }
    return true;
}

bool ZmqCommandServer::running() const noexcept { return impl_->active; }

std::string ZmqCommandServer::bound_endpoint() const { return impl_->endpoint; }

void ZmqCommandServer::stop() noexcept {
    impl_->stopping = true;
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->endpoint.clear();
}

}  // namespace ems::messaging

#include <rtdb/client/command_client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <nlohmann/json.hpp>

#ifdef EMS_RTDB_WITH_ZEROMQ
#include <common/messaging/zmq_change_channel.h>
#endif

namespace ems::rtdb {
namespace {
using Json = nlohmann::json;

std::int64_t utc_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool same_request(const CommandRequest& a, const CommandRequest& b) {
    return a.command_id == b.command_id && a.point_id == b.point_id && a.value == b.value &&
           a.config_revision == b.config_revision && a.expires_at_ms == b.expires_at_ms;
}

bool valid_request(const CommandRequest& request, const Scope& scope) {
    return validate_record_key({scope, RecordKind::CommandState, {}, request.command_id}).empty() &&
           !request.point_id.empty() && request.point_id.size() <= 64 &&
           std::isfinite(request.value) && request.config_revision >= 0 &&
           request.expires_at_ms > 0;
}

bool valid_status(const std::string& status) {
    for (const auto* allowed : {"Pending", "Queued", "Executing", "Acked", "Succeeded", "Failed",
                                "Expired", "Rejected", "Unknown"})
        if (status == allowed) return true;
    return false;
}

Fields encode(const CommandRecord& record) {
    const auto& request = record.request;
    return {{"json", Json{{"command_id", request.command_id},
                          {"point_id", request.point_id},
                          {"value", request.value},
                          {"config_revision", request.config_revision},
                          {"expires_at_ms", request.expires_at_ms},
                          {"status", record.status},
                          {"reason", record.reason},
                          {"sent_at_ms", record.sent_at_ms},
                          {"updated_at_ms", record.updated_at_ms},
                          {"verify_requested", record.verify_requested}}
                         .dump()}};
}
}  // namespace

struct CommandClient::Impl {
    std::unique_ptr<RealtimeClient> realtime;
    Scope scope;
#ifdef EMS_RTDB_WITH_ZEROMQ
    std::unique_ptr<messaging::ZmqChangePublisher> publisher;
#endif

    RecordKey key(const std::string& id) const { return {scope, RecordKind::CommandState, {}, id}; }

    bool notify(const char* topic, const std::string& id) {
#ifdef EMS_RTDB_WITH_ZEROMQ
        return publisher && publisher->publish(topic, id);
#else
        return false;
#endif
    }
};

CommandClient::CommandClient(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

CommandClient::~CommandClient() = default;

Result<std::shared_ptr<CommandClient>> CommandClient::open(const std::string& config_path,
                                                           Scope scope, const std::string& endpoint,
                                                           const CallContext& ctx) {
    if (!validate_record_key({scope, RecordKind::CommandState, {}, "probe"}).empty())
        return Error{ErrorCode::InvalidArgument, WriteEffect::NotApplied, "invalid command scope"};
    auto opened = RealtimeClient::open(config_path, ctx);
    if (!core::is_ok(opened)) return core::error_of(opened);
    auto impl = std::make_unique<Impl>();
    impl->realtime = std::move(std::get<0>(opened));
    impl->scope = std::move(scope);
    if (!endpoint.empty()) {
#ifdef EMS_RTDB_WITH_ZEROMQ
        messaging::ZmqChangeConfig config;
        config.endpoint = endpoint;
        config.publisher_bind = false;
        impl->publisher = std::make_unique<messaging::ZmqChangePublisher>();
        if (!impl->publisher->start(config))
            return Error{ErrorCode::Unavailable, WriteEffect::NotApplied,
                         "command notification publisher unavailable"};
#else
        return Error{ErrorCode::Unsupported, WriteEffect::NotApplied,
                     "command notifications require ZeroMQ"};
#endif
    }
    return std::shared_ptr<CommandClient>(new CommandClient(std::move(impl)));
}

Result<CommandRecord> CommandClient::readCommand(const std::string& id, const CallContext& ctx) {
    auto result = impl_->realtime->read(impl_->key(id), ctx);
    if (!core::is_ok(result)) return core::error_of(result);
    try {
        const auto& stored = core::value_of(result);
        const auto json = Json::parse(stored.fields.at("json"));
        CommandRecord record;
        auto& request = record.request;
        request.command_id = json.at("command_id").get<std::string>();
        request.point_id = json.at("point_id").get<std::string>();
        request.value = json.at("value").get<double>();
        request.config_revision = json.at("config_revision").get<std::int64_t>();
        request.expires_at_ms = json.at("expires_at_ms").get<std::int64_t>();
        record.status = json.at("status").get<std::string>();
        record.reason = json.at("reason").get<std::string>();
        record.sent_at_ms = json.at("sent_at_ms").get<std::int64_t>();
        record.updated_at_ms = json.at("updated_at_ms").get<std::int64_t>();
        record.verify_requested = json.at("verify_requested").get<bool>();
        record.revision = stored.revision;
        if (request.command_id == id && valid_request(request, impl_->scope) &&
            valid_status(record.status) && record.reason.size() <= 512 && record.sent_at_ms >= 0 &&
            record.updated_at_ms > 0)
            return record;
    } catch (const std::exception&) {
    }
    return Error{ErrorCode::CorruptData, WriteEffect::NotApplicable, "invalid command record"};
}

Result<CommandSubmission> CommandClient::submitCommand(const CommandRequest& request,
                                                       const CallContext& ctx) {
    if (!valid_request(request, impl_->scope))
        return Error{ErrorCode::InvalidArgument, WriteEffect::NotApplied,
                     "invalid command request"};
    // 必须先识别重复：原命令过期后，仍允许用原 ID/原期限查询既有受理结果。
    auto existing = readCommand(request.command_id, ctx);
    if (core::is_ok(existing)) {
        if (!same_request(core::value_of(existing).request, request))
            return Error{ErrorCode::Conflict, WriteEffect::NotApplied,
                         "command_id_payload_conflict"};
        return CommandSubmission{core::value_of(existing), true,
                                 impl_->notify("command.pending", request.command_id)};
    }
    if (core::error_of(existing).code != ErrorCode::NotFound) return core::error_of(existing);
    const auto now = utc_ms();
    if (request.expires_at_ms <= now || request.expires_at_ms > now + 3600000)
        return Error{ErrorCode::InvalidArgument, WriteEffect::NotApplied,
                     "invalid command deadline"};
    CommandRecord record;
    record.request = request;
    record.updated_at_ms = now;
    auto written = impl_->realtime->compareExchange(
        {impl_->key(request.command_id), encode(record), std::nullopt}, ctx);
    if (!core::is_ok(written)) {
        if (core::error_of(written).code == ErrorCode::Conflict) return submitCommand(request, ctx);
        return core::error_of(written);
    }
    record.revision = core::value_of(written).revision;
    return CommandSubmission{record, false, impl_->notify("command.pending", request.command_id)};
}

Result<std::vector<std::string>> CommandClient::listCommands(const CallContext& ctx) {
    auto keys = impl_->realtime->listCommandStates(impl_->scope, ctx);
    if (!core::is_ok(keys)) return core::error_of(keys);
    std::vector<std::string> ids;
    for (const auto& key : core::value_of(keys)) ids.push_back(key.name);
    std::sort(ids.begin(), ids.end());
    return ids;
}

Result<CommandRecord> CommandClient::updateCommand(const CommandRecord& record,
                                                   const CallContext& ctx) {
    if (!valid_request(record.request, impl_->scope) || !valid_status(record.status) ||
        record.reason.size() > 512 || record.sent_at_ms < 0 || record.updated_at_ms <= 0)
        return Error{ErrorCode::InvalidArgument, WriteEffect::NotApplied, "invalid command record"};
    if (!record.revision.empty()) {
        auto current = readCommand(record.request.command_id, ctx);
        if (!core::is_ok(current)) return core::error_of(current);
        if (!same_request(record.request, core::value_of(current).request))
            return Error{ErrorCode::Conflict, WriteEffect::NotApplied,
                         "command target is immutable"};
    }
    auto written = impl_->realtime->compareExchange(
        {impl_->key(record.request.command_id), encode(record),
         record.revision.empty() ? std::nullopt : std::optional<Revision>{record.revision}},
        ctx);
    if (!core::is_ok(written)) return core::error_of(written);
    auto result = record;
    result.revision = core::value_of(written).revision;
    return result;
}

Result<CommandRecord> CommandClient::requestVerification(const std::string& id,
                                                         const CallContext& ctx) {
    auto current = readCommand(id, ctx);
    if (!core::is_ok(current)) return core::error_of(current);
    auto record = core::value_of(current);
    if (record.status != "Acked" && record.status != "Unknown")
        return Error{ErrorCode::Conflict, WriteEffect::NotApplied,
                     "command does not require verification"};
    record.verify_requested = true;
    auto result = updateCommand(record, ctx);
    if (core::is_ok(result)) impl_->notify("command.pending", id);
    return result;
}
}  // namespace ems::rtdb

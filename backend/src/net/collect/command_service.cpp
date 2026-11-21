#include "command_service.h"

#include <chrono>
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace ems::net::collect {
namespace {
using Json = nlohmann::json;
using Row = database::CollectorCommandRow;
using Reply = messaging::ZmqCommandReply;
using Code = messaging::ZmqCommandReplyCode;

std::int64_t utc_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

Json result_json(const Row& row) {
    return {{"command_id", row.command_id},
            {"point_id", row.point_id},
            {"config_revision", row.config_revision},
            {"status", row.status},
            {"reason", row.reason},
            {"sent_at_ms", row.sent_at_ms},
            {"updated_at_ms", row.updated_at_ms}};
}

bool valid_id(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    for (const unsigned char ch : id)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
              ch == '-' || ch == '_'))
            return false;
    return true;
}
}  // namespace

struct CommandService::Journal {
    std::shared_ptr<database::IDatabase> database;
    std::shared_ptr<database::ICollectorCommandRepository> repository;
    std::string scope;

    template <class T, class Op>
    core::Result<T> transaction(Op op) {
        const auto ctx = core::make_call_context(std::chrono::seconds(2));
        auto begun = database->begin(ctx);
        if (!core::is_ok(begun)) return core::error_of(begun);
        auto tx = std::move(std::get<0>(begun));
        auto result = op(*tx, ctx);
        if (!core::is_ok(result)) return core::error_of(result);
        auto committed = tx->commit(ctx);
        if (!core::is_ok(committed)) return core::error_of(committed);
        return result;
    }

    core::Result<std::optional<Row>> get(const std::string& id) {
        return transaction<std::optional<Row>>(
            [&](auto& tx, const auto& ctx) { return repository->find(tx, scope, id, false, ctx); });
    }

    core::Result<std::vector<Row>> pending(std::int64_t after) {
        return transaction<std::vector<Row>>(
            [&](auto& tx, const auto& ctx) { return repository->pending(tx, scope, after, ctx); });
    }

    core::Result<bool> change(const std::string& id, const std::string& expected,
                              const PointCommandResult& result, bool mark_send = false) {
        return transaction<bool>([&](auto& tx, const auto& ctx) -> core::Result<bool> {
            auto existing = repository->find(tx, scope, id, true, ctx);
            if (!core::is_ok(existing)) return core::error_of(existing);
            auto row = core::value_of(existing);
            if (!row || row->status != expected) return false;
            row->status = result.status;
            row->reason = result.reason.substr(0, 512);
            row->updated_at_ms = utc_ms();
            if (mark_send) row->sent_at_ms = row->updated_at_ms;
            if (result.sent_at_ms > 0) row->sent_at_ms = result.sent_at_ms;
            auto saved = repository->save(tx, *row, ctx);
            if (!core::is_ok(saved)) return core::error_of(saved);
            return true;
        });
    }
};

CommandService::CommandService(std::shared_ptr<database::IDatabase> database,
                               std::shared_ptr<database::ICollectorCommandRepository> repository,
                               CommandRouter& engine, std::string scope)
    : journal_(std::make_shared<Journal>(
          Journal{std::move(database), std::move(repository), std::move(scope)})),
      engine_(engine) {
    if (!journal_->database || !journal_->repository || journal_->scope.empty())
        throw std::invalid_argument("command service requires database, repository and scope");
    // 前缀隔离不同进程实例；北向控制 ID 在插件内单调递增，重连后会从头开始。
    peer_prefix_ = "peer104-" + std::to_string(utc_ms()) + "-";
}

CommandService::~CommandService() { set_accepting(false); }

void CommandService::set_accepting(bool accepting) { accepting_->store(accepting); }

bool CommandService::recover() {
    set_accepting(false);
    std::int64_t after = 0;
    while (true) {
        auto page = journal_->pending(after);
        if (!core::is_ok(page)) return false;
        for (const auto& row : core::value_of(page)) {
            after = row.id;
            if (row.status != "Executing") continue;
            // 只能证明曾登记发送意图。核验需使用恢复后新收到的反馈，不重发。
            auto changed =
                journal_->change(row.command_id, "Executing",
                                 {"Unknown", "collector_restarted_during_execution"}, true);
            if (!core::is_ok(changed)) return false;
        }
        if (core::value_of(page).size() < 128) return true;
    }
}

Reply CommandService::handle(const messaging::ZmqCommandRequest& request) {
    try {
        if (!valid_id(request.command_id) || request.payload_json.size() > 2048)
            return {Code::Invalid, "invalid_command_id_or_payload_size"};
        auto payload = Json::parse(request.payload_json);
        if (!payload.is_object()) return {Code::Invalid, "command_payload_must_be_object"};
        const auto operation = payload.value("op", std::string("submit"));
        auto found = journal_->get(request.command_id);
        if (!core::is_ok(found)) return {Code::Unknown, "command_database_unavailable"};
        const auto& existing = core::value_of(found);
        if (operation == "query" || operation == "verify") {
            if (payload.size() != 1) return {Code::Invalid, "query_or_verify_accepts_only_op"};
            if (!existing) return {Code::Rejected, "command_not_found"};
            if (operation == "verify") {
                if (!accepting_->load()) return {Code::RetryLater, "collector_not_ready"};
                if (existing->status != "Unknown" && existing->status != "Acked")
                    return {Code::Rejected, "command_does_not_require_verification"};
                if (existing->config_revision != engine_.config_revision())
                    return {Code::Rejected, "config_revision_changed"};
                const auto channel = engine_.command_channel(existing->point_id);
                const auto route =
                    channel ? channel->command_route(existing->point_id) : std::nullopt;
                if (!route || route->feedback_json.empty())
                    return {Code::Rejected, "feedback_route_not_configured"};
                if (!schedule(*existing, true)) return {Code::RetryLater, "verification_not_ready"};
            }
            return {Code::Result, result_json(*existing).dump()};
        }
        if (operation != "submit") return {Code::Invalid, "unsupported_command_operation"};
        for (auto it = payload.begin(); it != payload.end(); ++it)
            if (it.key() != "op" && it.key() != "point_id" && it.key() != "value" &&
                it.key() != "config_revision" && it.key() != "expires_at_ms")
                return {Code::Invalid, "unknown_command_field"};
        if (!payload.at("point_id").is_string() || !payload.at("value").is_number() ||
            !payload.at("config_revision").is_number_integer() ||
            !payload.at("expires_at_ms").is_number_integer())
            return {Code::Invalid, "invalid_command_field_type"};
        Row row;
        row.command_id = request.command_id;
        row.scope_code = journal_->scope;
        row.point_id = payload.at("point_id").get<std::string>();
        row.value = payload.at("value").get<double>();
        row.config_revision = payload.at("config_revision").get<std::int64_t>();
        row.expires_at_ms = payload.at("expires_at_ms").get<std::int64_t>();
        if (row.point_id.empty() || row.point_id.size() > 64 || !std::isfinite(row.value) ||
            row.config_revision < 0 || row.expires_at_ms <= 0 ||
            row.expires_at_ms > utc_ms() + 3600000)
            return {Code::Invalid, "invalid_command_value_or_deadline"};
        payload["op"] = "submit";
        payload["value"] = row.value;
        row.payload_json = payload.dump();
        if (existing) {
            if (existing->payload_json != row.payload_json)
                return {Code::Rejected, "command_id_payload_conflict"};
            return {Code::Duplicate, result_json(*existing).dump()};
        }
        if (!accepting_->load()) return {Code::RetryLater, "collector_not_ready"};
        row.updated_at_ms = utc_ms();
        row.status = "Queued";
        if (row.expires_at_ms <= row.updated_at_ms) {
            row.status = "Expired";
            row.reason = "expired_before_acceptance";
        } else if (row.config_revision != engine_.config_revision()) {
            row.status = "Rejected";
            row.reason = "config_revision_mismatch";
        } else if (!engine_.command_channel(row.point_id)) {
            row.status = "Rejected";
            row.reason = "control_point_or_protocol_not_available";
        }
        auto saved = journal_->transaction<core::Unit>(
            [&](auto& tx, const auto& ctx) { return journal_->repository->save(tx, row, ctx); });
        if (!core::is_ok(saved)) {
            // 并发唯一键冲突或提交回复丢失：调用方用相同 ID 查询，不能换 ID 重发。
            return {Code::Unknown, "command_registration_not_confirmed"};
        }
        // 提交后直接入队，避免主循环配置读取延迟控制；队满时由持久扫描补入。
        if (row.status == "Queued") schedule(row, false);
        return {row.status == "Queued" ? Code::Accepted : Code::Rejected, result_json(row).dump()};
    } catch (const Json::exception&) {
        return {Code::Invalid, "invalid_command_json"};
    }
}

bool CommandService::schedule(const Row& row, bool verify) {
    if (!accepting_->load() || row.config_revision != engine_.config_revision()) return false;
    auto channel = engine_.command_channel(row.point_id);
    if (!channel) return false;
    {
        std::lock_guard lock(active_->mutex);
        if (!active_->ids.insert(row.command_id).second) return true;
    }
    PointCommand command{row.command_id,      row.point_id, row.value,     row.expires_at_ms,
                         row.config_revision, verify,       row.sent_at_ms};
    auto journal = journal_;
    auto active = active_;
    auto accepting = accepting_;
    auto* engine = &engine_;
    const bool queued = channel->enqueue_command(
        command,
        [journal, accepting, engine, row, verify] {
            if (!accepting->load() || !engine->is_running() ||
                row.config_revision != engine->config_revision())
                return false;
            if (verify) {
                auto current = journal->get(row.command_id);
                return core::is_ok(current) && core::value_of(current) &&
                       core::value_of(current)->status == row.status;
            }
            if (utc_ms() >= row.expires_at_ms) return false;
            auto registered = journal->change(row.command_id, "Queued",
                                              {"Executing", "send_intent_registered"}, true);
            return core::is_ok(registered) && core::value_of(registered);
        },
        [journal, active, accepting, row, verify](const PointCommandResult& outcome) {
            const auto result = outcome.status == "Deferred" && !accepting->load()
                                    ? PointCommandResult{"Rejected", "collector_paused_before_send"}
                                    : outcome;
            if (result.status != "Deferred") {
                if (verify) {
                    // 停机取消核验不能把原命令判为未执行或失败。
                    if (result.status == "Succeeded")
                        journal->change(row.command_id, row.status, result);
                } else {
                    auto changed = journal->change(row.command_id, "Executing", result);
                    if (core::is_ok(changed) && !core::value_of(changed))
                        journal->change(row.command_id, "Queued", result);
                }
            }
            ChannelThread::PeerOutcomeHandler peer;
            {
                std::lock_guard lock(active->mutex);
                active->ids.erase(row.command_id);
                const auto found = active->peers.find(row.command_id);
                if (found != active->peers.end()) {
                    peer = std::move(found->second);
                    active->peers.erase(found);
                }
            }
            // 北向控制回执：只有协议确认或反馈满足才算执行成功。
            if (peer) {
                const bool accepted = result.status == "Acked" || result.status == "Succeeded";
                peer(accepted, result.status + (result.reason.empty() ? "" : ": " + result.reason));
            }
        });
    if (!queued) {
        ChannelThread::PeerOutcomeHandler peer;
        {
            std::lock_guard lock(active_->mutex);
            active_->ids.erase(row.command_id);
            const auto found = active_->peers.find(row.command_id);
            if (found != active_->peers.end()) {
                peer = std::move(found->second);
                active_->peers.erase(found);
            }
        }
        // 入队失败不会有完成回调，必须在这里结束北向请求，不能留在待定状态。
        if (peer) peer(false, "command_queue_full");
        return false;
    }
    return queued;
}

bool CommandService::submit_peer(std::int64_t channel_id, const ChannelThread::PeerControl& control,
                                 ChannelThread::PeerOutcomeHandler done) {
    if (!done) return false;
    bool accepted = false;
    {
        std::lock_guard lock(peer_mutex_);
        // 有界队列：协议侧已回正 ACT_CON，超限必须尽快给出负结束确认。
        if (peer_queue_.size() < 256) {
            peer_queue_.push_back({channel_id, control, done});
            accepted = true;
        }
    }
    if (!accepted) done(false, "peer_control_queue_full");
    return accepted;
}

bool CommandService::drain_peers() {
    std::deque<PeerRequest> batch;
    {
        std::lock_guard lock(peer_mutex_);
        batch.swap(peer_queue_);
    }
    for (auto& item : batch) accept_peer(std::move(item));
    return true;
}

void CommandService::accept_peer(PeerRequest request) {
    auto done = std::move(request.done);
    const auto id = peer_prefix_ + std::to_string(request.channel_id) + "-" +
                    std::to_string(request.control.control_id);
    const Json payload = {{"op", "submit"},
                          {"point_id", request.control.point_id},
                          {"value", request.control.value},
                          {"config_revision", engine_.config_revision()},
                          {"expires_at_ms", request.control.expires_at_ms}};
    // 先登记回执再受理：受理成功时完成回调可能在本函数返回前发生。
    {
        std::lock_guard lock(active_->mutex);
        active_->peers[id] = std::move(done);
    }
    const auto reply = handle({id, payload.dump()});
    if (reply.code == Code::Accepted) return;
    ChannelThread::PeerOutcomeHandler peer;
    {
        std::lock_guard lock(active_->mutex);
        const auto found = active_->peers.find(id);
        if (found != active_->peers.end()) {
            peer = std::move(found->second);
            active_->peers.erase(found);
        }
    }
    // Duplicate 说明同 ID 记录已存在且可能仍在执行，本次无法据其确认执行结果。
    if (peer) peer(false, reply.code == Code::Duplicate ? "duplicate_peer_control" : reply.reason);
}

bool CommandService::poll() {
    drain_peers();
    if (!accepting_->load()) return true;
    auto page = journal_->pending(cursor_);
    if (!core::is_ok(page)) return false;
    for (const auto& row : core::value_of(page)) {
        cursor_ = row.id;
        if (row.status == "Queued" && utc_ms() >= row.expires_at_ms) {
            auto changed =
                journal_->change(row.command_id, "Queued", {"Expired", "expired_before_send"});
            if (!core::is_ok(changed)) return false;
            continue;
        }
        {
            std::lock_guard lock(active_->mutex);
            if (active_->ids.count(row.command_id)) continue;
        }
        if (row.status == "Executing") {
            auto changed = journal_->change(row.command_id, "Executing",
                                            {"Unknown", "execution_result_not_persisted"}, true);
            if (!core::is_ok(changed)) return false;
            continue;
        }
        if (row.status == "Queued") {
            if (row.config_revision != engine_.config_revision()) {
                auto changed = journal_->change(row.command_id, "Queued",
                                                {"Rejected", "config_revision_changed"});
                if (!core::is_ok(changed)) return false;
            } else if (!engine_.command_channel(row.point_id)) {
                auto changed = journal_->change(row.command_id, "Queued",
                                                {"Rejected", "control_route_removed"});
                if (!core::is_ok(changed)) return false;
            } else {
                schedule(row, false);
            }
            continue;
        }
        auto channel = engine_.command_channel(row.point_id);
        auto route = channel ? channel->command_route(row.point_id) : std::nullopt;
        if (!route || row.config_revision != engine_.config_revision() ||
            route->feedback_json.empty())
            continue;
        if (utc_ms() - row.sent_at_ms > route->feedback_timeout_ms) {
            if (row.status == "Acked") {
                auto changed =
                    journal_->change(row.command_id, "Acked", {"Unknown", "feedback_timeout"});
                if (!core::is_ok(changed)) return false;
            }
            continue;
        }
        schedule(row, true);
    }
    if (core::value_of(page).size() < 128) cursor_ = 0;
    return true;
}
}  // namespace ems::net::collect

#include "command_service.h"

#include <chrono>
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace ems::net::collect {
namespace {
using Json = nlohmann::json;
using Row = database::CollectorCommandRow;

std::int64_t utc_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

struct CommandService::Journal {
    std::shared_ptr<database::IDatabase> database;
    std::shared_ptr<database::ICollectorCommandRepository> repository;
    std::string scope;
    std::shared_ptr<rtdb::CommandClient> realtime;
    std::mutex sync_mutex;
    std::set<std::string> dirty;

    Journal(std::shared_ptr<database::IDatabase> db,
            std::shared_ptr<database::ICollectorCommandRepository> repo, std::string scope_code,
            std::shared_ptr<rtdb::CommandClient> client)
        : database(std::move(db)),
          repository(std::move(repo)),
          scope(std::move(scope_code)),
          realtime(std::move(client)) {}

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

    // 持久事实优先：串行刷新当前数据库行，防止迟到的回调覆盖较新的实时状态。
    bool sync_locked(const std::string& id) {
        auto found = get(id);
        if (!core::is_ok(found) || !core::value_of(found)) return false;
        const auto& row = *core::value_of(found);
        const auto ctx = core::make_call_context(std::chrono::seconds(2));
        for (int attempt = 0; attempt < 4; ++attempt) {
            auto current = realtime->readCommand(id, ctx);
            rtdb::CommandRecord record;
            if (core::is_ok(current)) {
                record = core::value_of(current);
                if (record.request.point_id != row.point_id || record.request.value != row.value ||
                    record.request.config_revision != row.config_revision ||
                    record.request.expires_at_ms != row.expires_at_ms) {
                    // 共享段清理后仍须服从持久去重，不能让重用的 ID 改变原动作。
                    if (record.status == "Rejected" &&
                        record.reason == "command_id_payload_conflict")
                        return true;
                    record.status = "Rejected";
                    record.reason = "command_id_payload_conflict";
                    record.updated_at_ms = utc_ms();
                    auto rejected = realtime->updateCommand(record, ctx);
                    if (core::is_ok(rejected)) return true;
                    if (core::error_of(rejected).code == core::ErrorCode::Conflict) continue;
                    return false;
                }
            } else if (core::error_of(current).code != core::ErrorCode::NotFound) {
                return false;
            }
            record.request = {row.command_id, row.point_id, row.value, row.config_revision,
                              row.expires_at_ms};
            if (record.status == row.status && record.reason == row.reason &&
                record.sent_at_ms == row.sent_at_ms && record.updated_at_ms == row.updated_at_ms &&
                !record.revision.empty())
                return true;
            record.status = row.status;
            record.reason = row.reason;
            record.sent_at_ms = row.sent_at_ms;
            record.updated_at_ms = row.updated_at_ms;
            auto updated = realtime->updateCommand(record, ctx);
            if (core::is_ok(updated)) return true;
            if (core::error_of(updated).code != core::ErrorCode::Conflict) return false;
        }
        return false;
    }

    bool sync(const std::string& id) {
        std::lock_guard lock(sync_mutex);
        dirty.insert(id);
        if (!sync_locked(id)) return false;
        dirty.erase(id);
        return true;
    }

    bool flush() {
        std::lock_guard lock(sync_mutex);
        std::size_t remaining = 128;
        for (auto it = dirty.begin(); it != dirty.end() && remaining--;) {
            if (!sync_locked(*it)) return false;
            it = dirty.erase(it);
        }
        return dirty.empty();
    }

    core::Result<bool> change(const std::string& id, const std::string& expected,
                              const PointCommandResult& result, bool mark_send = false) {
        std::lock_guard lock(sync_mutex);
        dirty.insert(id);
        auto changed = transaction<bool>([&](auto& tx, const auto& ctx) -> core::Result<bool> {
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
        if (!core::is_ok(changed)) {
            // CAS 已认领但持久登记失败：重新读取持久事实，不能保留虚假的执行中状态。
            if (sync_locked(id)) dirty.erase(id);
            return core::error_of(changed);
        }
        if (!sync_locked(id))
            return core::make_error(core::ErrorCode::Unavailable, core::WriteEffect::Unknown,
                                    "command state synchronization pending");
        dirty.erase(id);
        return changed;
    }
};

CommandService::CommandService(std::shared_ptr<database::IDatabase> database,
                               std::shared_ptr<database::ICollectorCommandRepository> repository,
                               CommandRouter& engine, std::string scope,
                               std::shared_ptr<rtdb::CommandClient> realtime)
    : journal_(std::make_shared<Journal>(std::move(database), std::move(repository),
                                         std::move(scope), std::move(realtime))),
      engine_(engine) {
    if (!journal_->database || !journal_->repository || !journal_->realtime ||
        journal_->scope.empty())
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
            if (!journal_->sync(row.command_id)) return false;
            if (row.status != "Executing") continue;
            // 只能证明曾登记发送意图。核验需使用恢复后新收到的反馈，不重发。
            auto changed =
                journal_->change(row.command_id, "Executing",
                                 {"Unknown", "collector_restarted_during_execution"}, true);
            if (!core::is_ok(changed)) return false;
        }
        if (core::value_of(page).size() < 128) break;
    }
    // 补齐上次已提交终态、尚未来得及写回实时库的记录；此类记录不在 pending 查询中。
    const auto ctx = core::make_call_context(std::chrono::seconds(5));
    auto ids = journal_->realtime->listCommands(ctx);
    if (!core::is_ok(ids)) return false;
    for (const auto& id : core::value_of(ids)) {
        auto row = journal_->get(id);
        if (!core::is_ok(row)) return false;
        if (core::value_of(row) && !journal_->sync(id)) return false;
    }
    return true;
}

bool CommandService::ingest(const rtdb::CommandRecord& command) {
    const auto& request = command.request;
    auto found = journal_->get(request.command_id);
    if (!core::is_ok(found)) return false;
    if (!core::value_of(found)) {
        Row row;
        row.command_id = request.command_id;
        row.scope_code = journal_->scope;
        row.point_id = request.point_id;
        row.value = request.value;
        row.config_revision = request.config_revision;
        row.expires_at_ms = request.expires_at_ms;
        row.payload_json = Json{{"op", "submit"},
                                {"point_id", row.point_id},
                                {"value", row.value},
                                {"config_revision", row.config_revision},
                                {"expires_at_ms", row.expires_at_ms}}
                               .dump();
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
        if (!core::is_ok(saved)) return false;
    }
    if (!journal_->sync(request.command_id)) return false;
    const auto ctx = core::make_call_context(std::chrono::seconds(2));
    auto current = journal_->realtime->readCommand(request.command_id, ctx);
    if (!core::is_ok(current)) return false;
    const auto& state = core::value_of(current);
    if (state.status == "Queued") {
        auto row = journal_->get(request.command_id);
        if (!core::is_ok(row)) return false;
        if (core::value_of(row)) schedule(*core::value_of(row), false);
    }
    if (state.status != "Queued" && state.status != "Executing") {
        ChannelThread::PeerOutcomeHandler peer;
        {
            std::lock_guard lock(active_->mutex);
            const auto it = active_->peers.find(request.command_id);
            if (it != active_->peers.end()) {
                peer = std::move(it->second);
                active_->peers.erase(it);
            }
        }
        if (peer) peer(state.status == "Acked" || state.status == "Succeeded", state.reason);
    }
    return true;
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
            const auto ctx = core::make_call_context(std::chrono::seconds(2));
            auto current = journal->realtime->readCommand(row.command_id, ctx);
            if (!core::is_ok(current)) return false;
            auto command = core::value_of(current);
            if (command.status != "Queued" || command.request.point_id != row.point_id ||
                command.request.value != row.value ||
                command.request.config_revision != row.config_revision ||
                command.request.expires_at_ms != row.expires_at_ms)
                return false;
            command.status = "Executing";
            command.updated_at_ms = utc_ms();
            if (!core::is_ok(journal->realtime->updateCommand(command, ctx))) return false;
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
    // 先登记回执再受理：受理成功时完成回调可能在本函数返回前发生。
    {
        std::lock_guard lock(active_->mutex);
        active_->peers[id] = std::move(done);
    }
    const auto ctx = core::make_call_context(std::chrono::seconds(2));
    const auto submitted = journal_->realtime->submitCommand(
        {id, request.control.point_id, request.control.value, engine_.config_revision(),
         request.control.expires_at_ms},
        ctx);
    if (core::is_ok(submitted) && !core::value_of(submitted).duplicate) return;
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
    if (peer)
        peer(false,
             core::is_ok(submitted) ? "duplicate_peer_control" : core::error_of(submitted).message);
}

void CommandService::notify(const std::string& id) {
    if (!rtdb::validate_record_key(
             {{rtdb::ScopeKind::Station, journal_->scope}, rtdb::RecordKind::CommandState, {}, id})
             .empty())
        return;
    std::lock_guard lock(hint_mutex_);
    if (hints_.size() < 1024) hints_.insert(id);
}

bool CommandService::poll() {
    drain_peers();
    if (!journal_->flush()) return false;
    if (!accepting_->load()) return true;
    const auto ctx = core::make_call_context(std::chrono::seconds(2));
    std::set<std::string> ready;
    {
        std::lock_guard lock(hint_mutex_);
        ready.swap(hints_);
    }
    auto ids = journal_->realtime->listCommands(ctx);
    if (!core::is_ok(ids)) return false;
    const auto& keys = core::value_of(ids);
    if (realtime_cursor_ >= keys.size()) realtime_cursor_ = 0;
    const auto end = std::min(keys.size(), realtime_cursor_ + 128);
    while (realtime_cursor_ < end) ready.insert(keys[realtime_cursor_++]);
    for (const auto& id : ready) {
        auto current = journal_->realtime->readCommand(id, ctx);
        if (!core::is_ok(current)) {
            if (core::error_of(current).code == core::ErrorCode::NotFound) continue;
            return false;
        }
        auto command = core::value_of(current);
        if (command.status == "Pending" && !ingest(command)) return false;
        if (command.verify_requested) {
            auto found = journal_->get(command.request.command_id);
            if (!core::is_ok(found)) return false;
            if (core::value_of(found)) {
                const auto& row = *core::value_of(found);
                if ((row.status == "Unknown" || row.status == "Acked") &&
                    row.config_revision == engine_.config_revision())
                    schedule(row, true);
            }
            command.verify_requested = false;
            auto cleared = journal_->realtime->updateCommand(command, ctx);
            if (!core::is_ok(cleared) && core::error_of(cleared).code != core::ErrorCode::Conflict)
                return false;
        }
    }
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
            if (!journal_->sync(row.command_id)) return false;
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

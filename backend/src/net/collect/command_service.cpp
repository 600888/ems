#include "command_service.h"

#include <algorithm>
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
    std::mutex state_mutex;
    std::map<std::string, std::weak_ptr<std::mutex>> command_mutexes;
    std::string dirty_cursor;

    std::shared_ptr<std::mutex> mutex_for(const std::string& id) {
        std::lock_guard lock(state_mutex);
        auto& weak = command_mutexes[id];
        auto mutex = weak.lock();
        if (!mutex) {
            mutex = std::make_shared<std::mutex>();
            weak = mutex;
        }
        return mutex;
    }

    void mark(const std::string& id, bool pending = true) {
        std::lock_guard lock(state_mutex);
        if (pending)
            dirty.insert(id);
        else
            dirty.erase(id);
    }

    bool is_dirty(const std::string& id) {
        std::lock_guard lock(state_mutex);
        return dirty.count(id) != 0;
    }

    std::vector<std::string> dirty_ids() {
        std::lock_guard lock(state_mutex);
        std::vector<std::string> result;
        auto it = dirty.upper_bound(dirty_cursor);
        if (it == dirty.end()) it = dirty.begin();
        const auto count = std::min<std::size_t>(128, dirty.size());
        for (std::size_t i = 0; i < count; ++i) {
            result.push_back(*it++);
            if (it == dirty.end()) it = dirty.begin();
        }
        if (!result.empty()) dirty_cursor = result.back();
        return result;
    }

    std::set<std::string> dirty;
    std::function<std::shared_ptr<rtdb::PointClient>()> point_client;
    std::map<std::string, ChannelThread::PeerOutcomeHandler> deferred_peers;

    bool project(const Row& row, const rtdb::CommandRecord& record) {
        auto client = point_client();
        if (!client) return false;
        auto result = client->projectCommand(record, row.id,
                                             core::make_call_context(std::chrono::seconds(2)));
        if (!core::is_ok(result)) return false;
        if (row.status == "Queued" || row.status == "Executing") return true;
        ChannelThread::PeerOutcomeHandler done;
        {
            std::lock_guard lock(state_mutex);
            const auto it = deferred_peers.find(row.command_id);
            if (it != deferred_peers.end()) {
                done = std::move(it->second);
                deferred_peers.erase(it);
            }
        }
        if (done)
            done(row.status == "Acked" || row.status == "Succeeded",
                 row.status + ": " + row.reason);
        return true;
    }

    void complete_peer(const std::string& id, ChannelThread::PeerOutcomeHandler peer) {
        if (!peer) return;
        auto mutex = mutex_for(id);
        std::lock_guard lock(*mutex);
        {
            std::lock_guard state_lock(state_mutex);
            deferred_peers[id] = std::move(peer);
        }
        mark(id);
        if (sync_locked(id)) mark(id, false);
    }

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

    // 同一命令串行刷新持久事实；不同设备的 I/O 不持有全局互斥锁。
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
                return project(row, record);
            record.status = row.status;
            record.reason = row.reason;
            record.sent_at_ms = row.sent_at_ms;
            record.updated_at_ms = row.updated_at_ms;
            auto updated = realtime->updateCommand(record, ctx);
            if (core::is_ok(updated)) return project(row, core::value_of(updated));
            if (core::error_of(updated).code != core::ErrorCode::Conflict) return false;
        }
        return false;
    }

    bool sync(const std::string& id) {
        auto mutex = mutex_for(id);
        std::lock_guard lock(*mutex);
        mark(id);
        if (!sync_locked(id)) return false;
        mark(id, false);
        return true;
    }

    core::Result<bool> change(const std::string& id, const std::string& expected,
                              const PointCommandResult& result, bool mark_send = false) {
        auto mutex = mutex_for(id);
        std::lock_guard lock(*mutex);
        mark(id);
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
            if (sync_locked(id)) mark(id, false);
            return core::error_of(changed);
        }
        if (!sync_locked(id))
            return core::make_error(core::ErrorCode::Unavailable, core::WriteEffect::Unknown,
                                    "command state synchronization pending");
        mark(id, false);
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
    journal_->point_client = [&engine] { return engine.point_client(); };
    peer_prefix_ = "peer104-" + std::to_string(utc_ms()) + "-";
    try {
        for (unsigned i = 0; i < 8; ++i) workers_.emplace_back([this] { worker_loop(); });
    } catch (...) {
        {
            std::lock_guard lock(work_->mutex);
            work_->stopping = true;
        }
        work_->ready.notify_all();
        for (auto& worker : workers_) worker.join();
        throw;
    }
}

CommandService::~CommandService() {
    set_accepting(false);
    {
        std::lock_guard lock(work_->mutex);
        work_->stopping = true;
    }
    work_->ready.notify_all();
    for (auto& worker : workers_) worker.join();
}

void CommandService::set_accepting(bool accepting) {
    accepting_->store(accepting);
    if (accepting) {
        work_->ready.notify_all();
        return;
    }
    std::unique_lock lock(work_->mutex);
    work_->ready_devices.clear();
    for (auto& [_, queue] : work_->queues) {
        queue.commands.clear();
        queue.scheduled.clear();
        queue.retry_wait = false;
        queue.repair_command.clear();
    }
    work_->drained.wait(lock, [this] { return work_->active_jobs == 0; });
}

std::vector<std::string> CommandService::configure_subscriptions(
    const rtdb::RuntimeCatalog& catalog) {
    std::lock_guard lock(work_->mutex);
    work_->subscriptions.clear();
    work_->subscriptions[rtdb::command_device_topic("")] = "";
    for (const auto& point : catalog.points())
        if (point.enabled && model::is_command(point.point_type) &&
            !point.control_device_id.empty())
            work_->subscriptions[rtdb::command_device_topic(point.control_device_id)] =
                point.control_device_id;
    std::vector<std::string> topics;
    for (const auto& [topic, _] : work_->subscriptions)
        if (!topic.empty()) topics.push_back(topic);
    return topics;
}

std::string CommandService::device_for(const std::string& id) const {
    if (auto client = engine_.point_client())
        if (const auto* point = client->catalog()->findPoint(core::PointId{id}))
            if (!point->control_device_id.empty()) return point->control_device_id;
    auto channel = engine_.command_channel(id);
    return channel ? channel->command_device(id) : std::string{};
}

bool CommandService::enqueue(const std::string& device, const std::string& id, bool recovering) {
    if (!rtdb::validate_record_key(
             {{rtdb::ScopeKind::Station, journal_->scope}, rtdb::RecordKind::CommandState, {}, id})
             .empty())
        return false;
    std::lock_guard lock(work_->mutex);
    if (work_->stopping || (!accepting_->load() && !recovering)) return false;
    auto& queue = work_->queues[device];
    if (queue.retry_wait && !queue.running && !queue.blocked &&
        std::chrono::steady_clock::now() >= queue.retry_after) {
        queue.retry_wait = false;
        work_->ready_devices.push_back(device);
        work_->ready.notify_one();
    }
    if (queue.scheduled.count(id)) return true;
    if (queue.scheduled.size() >= 128) return false;
    queue.scheduled.insert(id);
    queue.commands.push_back(id);
    work_->known_devices[id] = device;
    if (!queue.running && !queue.blocked && !queue.retry_wait && queue.commands.size() == 1)
        work_->ready_devices.push_back(device);
    work_->ready.notify_one();
    return true;
}

void CommandService::worker_loop() {
    while (true) {
        std::string device, id;
        {
            std::unique_lock lock(work_->mutex);
            work_->ready.wait(lock, [this] {
                return work_->stopping || (accepting_->load() && !work_->ready_devices.empty());
            });
            if (work_->stopping) return;
            device = std::move(work_->ready_devices.front());
            work_->ready_devices.pop_front();
            auto& queue = work_->queues.at(device);
            id = std::move(queue.commands.front());
            queue.commands.pop_front();
            queue.running = true;
            ++work_->active_jobs;
        }
        bool ok = true;
        try {
            if (accepting_->load()) ok = process(device, id);
        } catch (...) {
            ok = false;
        }
        if (!ok) worker_error_.store(true);
        {
            std::lock_guard lock(work_->mutex);
            auto& queue = work_->queues.at(device);
            if (!ok && accepting_->load()) {
                queue.commands.push_front(id);
                queue.retry_wait = true;
                queue.retry_after =
                    std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
            } else
                queue.scheduled.erase(id);
            if (!queue.repair_command.empty() && accepting_->load()) {
                const auto repair = std::move(queue.repair_command);
                queue.repair_command.clear();
                queue.commands.erase(
                    std::remove(queue.commands.begin(), queue.commands.end(), repair),
                    queue.commands.end());
                queue.scheduled.insert(repair);
                queue.commands.push_front(repair);
            }
            queue.running = false;
            --work_->active_jobs;
            if (!queue.commands.empty() && !queue.blocked && !queue.retry_wait)
                work_->ready_devices.push_back(device);
        }
        work_->drained.notify_all();
        work_->ready.notify_one();
    }
}

bool CommandService::recover() {
    set_accepting(false);
    std::int64_t after = 0;
    while (true) {
        auto page = journal_->pending(after);
        if (!core::is_ok(page)) return false;
        for (const auto& row : core::value_of(page)) {
            after = row.id;
            if (!journal_->sync(row.command_id)) return false;
            enqueue(device_for(row.point_id), row.command_id, true);
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
    std::vector<rtdb::CommandRecord> pending;
    for (const auto& id : core::value_of(ids)) {
        auto record = journal_->realtime->readCommand(id, ctx);
        if (!core::is_ok(record)) return false;
        if (core::value_of(record).status == "Pending") pending.push_back(core::value_of(record));
        auto row = journal_->get(id);
        if (!core::is_ok(row)) return false;
        if (core::value_of(row) && !journal_->sync(id)) return false;
    }
    std::stable_sort(pending.begin(), pending.end(), [](const auto& a, const auto& b) {
        return a.updated_at_ms < b.updated_at_ms;
    });
    for (const auto& record : pending)
        enqueue(device_for(record.request.point_id), record.request.command_id, true);
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
    const auto device_id = channel->command_device(row.point_id);
    if (device_id.empty()) return false;
    {
        std::lock_guard lock(active_->mutex);
        if (active_->ids.count(row.command_id)) return true;
        if (active_->devices.count(device_id)) return false;
        active_->ids.insert(row.command_id);
        active_->devices[device_id] = row.command_id;
    }
    PointCommand command{row.command_id,      row.point_id, row.value,     row.expires_at_ms,
                         row.config_revision, verify,       row.sent_at_ms};
    auto journal = journal_;
    auto active = active_;
    auto accepting = accepting_;
    auto* engine = &engine_;
    auto work = work_;
    {
        std::lock_guard lock(work->mutex);
        work->queues[device_id].blocked = true;
    }
    auto release_device = [work, device_id, journal, accepting, id = row.command_id] {
        const bool pending_sync = journal->is_dirty(id);
        std::lock_guard lock(work->mutex);
        auto& queue = work->queues[device_id];
        queue.blocked = false;
        if (pending_sync && accepting->load() && !work->stopping) {
            if (queue.running)
                queue.repair_command = id;
            else {
                queue.commands.erase(std::remove(queue.commands.begin(), queue.commands.end(), id),
                                     queue.commands.end());
                queue.scheduled.insert(id);
                queue.commands.push_front(id);
            }
        }
        if (!work->stopping && !queue.running && !queue.commands.empty() && !queue.retry_wait) {
            work->ready_devices.push_back(device_id);
            work->ready.notify_one();
        }
    };
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
        [journal, active, accepting, row, verify, device_id,
         release_device](const PointCommandResult& outcome) {
            // 完成回调异常时也必须释放设备占用，持久事实由补同步队列修复。
            int scope{0};
            auto cleanup = [active, device_id, id = row.command_id, release_device](int*) {
                {
                    std::lock_guard lock(active->mutex);
                    active->ids.erase(id);
                    const auto busy = active->devices.find(device_id);
                    if (busy != active->devices.end() && busy->second == id)
                        active->devices.erase(busy);
                }
                release_device();
            };
            std::unique_ptr<int, decltype(cleanup)> finish(&scope, cleanup);
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
                const auto busy = active->devices.find(device_id);
                if (busy != active->devices.end() && busy->second == row.command_id)
                    active->devices.erase(busy);
                const auto found = active->peers.find(row.command_id);
                if (found != active->peers.end()) {
                    peer = std::move(found->second);
                    active->peers.erase(found);
                }
            }
            // 北向控制回执：只有协议确认或反馈满足才算执行成功。
            if (peer) journal->complete_peer(row.command_id, std::move(peer));
        });
    if (!queued) {
        release_device();
        ChannelThread::PeerOutcomeHandler peer;
        {
            std::lock_guard lock(active_->mutex);
            active_->ids.erase(row.command_id);
            const auto busy = active_->devices.find(device_id);
            if (busy != active_->devices.end() && busy->second == row.command_id)
                active_->devices.erase(busy);
            const auto found = active_->peers.find(row.command_id);
            if (found != active_->peers.end()) {
                peer = std::move(found->second);
                active_->peers.erase(found);
            }
        }
        // 入队失败不会有完成回调，必须在这里结束北向请求，不能留在待定状态。
        if (peer) {
            journal_->change(row.command_id, "Queued", {"Rejected", "command_queue_full"});
            journal_->complete_peer(row.command_id, std::move(peer));
        }
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
        if (accepting_->load() && peer_queue_.size() < 256) {
            peer_queue_.push_back({channel_id, control, done, engine_.config_revision()});
            accepted = true;
        }
    }
    if (!accepted)
        done(false, accepting_->load() ? "peer_control_queue_full" : "collector_not_accepting");
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
    const auto submitted =
        journal_->realtime->submitCommand({id, request.control.point_id, request.control.value,
                                           request.config_revision, request.control.expires_at_ms},
                                          ctx);
    if (core::is_ok(submitted) && !core::value_of(submitted).duplicate) {
        enqueue(device_for(request.control.point_id), id);
        return;
    }
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
    std::string device;
    {
        std::lock_guard lock(work_->mutex);
        const auto it = work_->known_devices.find(id);
        if (it != work_->known_devices.end()) device = it->second;
    }
    enqueue(device, id);
}

void CommandService::notify(const std::string& topic, const std::string& id) {
    std::string device;
    {
        std::lock_guard lock(work_->mutex);
        const auto it = work_->subscriptions.find(topic);
        if (it == work_->subscriptions.end()) return;
        device = it->second;
    }
    enqueue(device, id);
}

bool CommandService::process(const std::string& device, const std::string& id) {
    const auto ctx = core::make_call_context(std::chrono::seconds(2));
    auto current = journal_->realtime->readCommand(id, ctx);
    if (!core::is_ok(current)) return core::error_of(current).code == core::ErrorCode::NotFound;
    auto command = core::value_of(current);
    const auto actual_device = device_for(command.request.point_id);
    if (device != actual_device) {
        enqueue(actual_device, id);
        return true;
    }
    if (command.status == "Pending") {
        if (!ingest(command)) return false;
        current = journal_->realtime->readCommand(id, ctx);
        if (!core::is_ok(current)) return false;
        command = core::value_of(current);
    }
    {
        std::lock_guard lock(active_->mutex);
        if (active_->ids.count(id)) return true;
    }
    auto found = journal_->get(id);
    if (!core::is_ok(found)) return false;
    if (!core::value_of(found)) return true;
    const auto row = *core::value_of(found);
    if (!journal_->sync(id)) return false;
    if (command.verify_requested) {
        if ((row.status == "Unknown" || row.status == "Acked") &&
            row.config_revision == engine_.config_revision() && !schedule(row, true))
            return true;
        auto latest = journal_->realtime->readCommand(id, ctx);
        if (!core::is_ok(latest)) return false;
        auto clear = core::value_of(latest);
        clear.verify_requested = false;
        auto cleared = journal_->realtime->updateCommand(clear, ctx);
        if (!core::is_ok(cleared) && core::error_of(cleared).code != core::ErrorCode::Conflict)
            return false;
    }
    if (row.status == "Queued") {
        if (utc_ms() >= row.expires_at_ms)
            return core::is_ok(journal_->change(id, "Queued", {"Expired", "expired_before_send"}));
        if (row.config_revision != engine_.config_revision())
            return core::is_ok(
                journal_->change(id, "Queued", {"Rejected", "config_revision_changed"}));
        if (!engine_.command_channel(row.point_id))
            return core::is_ok(
                journal_->change(id, "Queued", {"Rejected", "control_route_removed"}));
        schedule(row, false);
        return true;
    }
    if (row.status == "Executing")
        return core::is_ok(
            journal_->change(id, "Executing", {"Unknown", "execution_result_not_persisted"}, true));
    auto channel = engine_.command_channel(row.point_id);
    auto route = channel ? channel->command_route(row.point_id) : std::nullopt;
    if (!route || row.config_revision != engine_.config_revision() ||
        route->feedback_json.empty() || (row.status != "Acked" && row.status != "Unknown"))
        return true;
    if (utc_ms() - row.sent_at_ms > route->feedback_timeout_ms) {
        if (row.status == "Acked")
            return core::is_ok(journal_->change(id, "Acked", {"Unknown", "feedback_timeout"}));
        return true;
    }
    schedule(row, true);
    return true;
}

bool CommandService::poll() {
    drain_peers();
    if (!accepting_->load()) return true;
    {
        std::lock_guard lock(work_->mutex);
        const auto now = std::chrono::steady_clock::now();
        for (auto& [device, queue] : work_->queues)
            if (queue.retry_wait && !queue.running && !queue.blocked && !queue.commands.empty() &&
                now >= queue.retry_after) {
                queue.retry_wait = false;
                work_->ready_devices.push_back(device);
            }
        work_->ready.notify_all();
    }
    for (const auto& id : journal_->dirty_ids()) notify(id);
    const auto ctx = core::make_call_context(std::chrono::seconds(2));
    auto ids = journal_->realtime->listCommands(ctx);
    if (!core::is_ok(ids)) return false;
    const auto& keys = core::value_of(ids);
    if (realtime_cursor_ >= keys.size()) realtime_cursor_ = 0;
    const auto end = std::min(keys.size(), realtime_cursor_ + 128);
    while (realtime_cursor_ < end) {
        const auto& id = keys[realtime_cursor_++];
        auto current = journal_->realtime->readCommand(id, ctx);
        if (!core::is_ok(current)) {
            if (core::error_of(current).code == core::ErrorCode::NotFound) continue;
            return false;
        }
        const auto& command = core::value_of(current);
        if (command.status == "Pending" || command.verify_requested)
            enqueue(device_for(command.request.point_id), id);
    }
    auto page = journal_->pending(cursor_);
    if (!core::is_ok(page)) return false;
    for (const auto& row : core::value_of(page)) {
        cursor_ = row.id;
        enqueue(device_for(row.point_id), row.command_id);
    }
    if (core::value_of(page).size() < 128) cursor_ = 0;
    // 为本轮快速受理保留有界等待；超时后的慢设备继续后台处理，不阻塞通知分发。
    {
        std::unique_lock lock(work_->mutex);
        work_->drained.wait_for(lock, std::chrono::milliseconds(20), [this] {
            return work_->ready_devices.empty() && work_->active_jobs == 0;
        });
    }
    return !worker_error_.exchange(false);
}
}  // namespace ems::net::collect

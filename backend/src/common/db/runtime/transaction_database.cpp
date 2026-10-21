#include "transaction_database.h"

#include <atomic>
#include <thread>
#include <utility>

namespace ems::database::detail {
namespace {
core::Error rejected(core::ErrorCode code, const char* message) {
    return core::make_error(code, core::WriteEffect::NotApplied, message);
}
}  // namespace

struct TransactionDatabase::State {
    TransactionOptions options;
    ConnectionFactory factory;
    std::atomic<bool> closing{false};

    void release(ConnectionLease lease, bool healthy) noexcept {
        lease.release(std::move(lease.connection), healthy);
    }
};

class TransactionDatabase::Transaction final : public ITransaction {
   public:
    Transaction(std::shared_ptr<State> state, ConnectionLease lease)
        : state_(std::move(state)), lease_(std::move(lease)), owner_(std::this_thread::get_id()) {}

    ~Transaction() override {
        if (lease_.connection) cleanup();
    }

    core::Result<CommitReceipt> commit(const core::CallContext& ctx) override {
        if (owner_ != std::this_thread::get_id() || !lease_.connection) {
            return rejected(core::ErrorCode::InvalidArgument,
                            "inactive or foreign-thread transaction");
        }
        if (invalid_) {
            const bool rolled_back = cleanup();
            return core::make_error(
                core::ErrorCode::Unavailable,
                rolled_back ? core::WriteEffect::NotApplied : core::WriteEffect::Unknown,
                "transaction is not committable");
        }
        if (core::has_expired(ctx)) {
            const auto rolled_back = cleanup();
            return core::make_error(
                core::ErrorCode::DeadlineExceeded,
                rolled_back ? core::WriteEffect::NotApplied : core::WriteEffect::Unknown,
                "transaction deadline before commit");
        }
        core::Status result;
        try {
            result = lease_.connection->commit(ctx);
        } catch (...) {
            result = core::make_error(core::ErrorCode::Unavailable, core::WriteEffect::Unknown,
                                      "database commit exception");
        }
        // A failed commit may have reached the server. Never return this connection to the pool.
        state_->release(std::move(lease_), core::is_ok(result));
        if (!core::is_ok(result)) {
            auto error = core::error_of(result);
            error.effect = core::WriteEffect::Unknown;
            return error;
        }
        return CommitReceipt{};
    }

    core::Status rollback(const core::CallContext& ctx) override {
        if (owner_ != std::this_thread::get_id()) {
            return rejected(core::ErrorCode::InvalidArgument, "foreign-thread transaction");
        }
        if (!lease_.connection) return core::ok_status();
        if (core::has_expired(ctx)) {
            const bool ok = cleanup();
            return core::make_error(core::ErrorCode::DeadlineExceeded,
                                    ok ? core::WriteEffect::NotApplied : core::WriteEffect::Unknown,
                                    "rollback deadline");
        }
        core::Status result;
        try {
            result = lease_.connection->rollback(ctx);
        } catch (...) {
            result = core::make_error(core::ErrorCode::Unavailable, core::WriteEffect::Unknown,
                                      "database rollback exception");
        }
        state_->release(std::move(lease_), core::is_ok(result) && !invalid_);
        return result;
    }

    bool cleanup() noexcept {
        bool healthy = false;
        try {
            healthy = core::is_ok(lease_.connection->rollback(
                core::make_call_context(state_->options.cleanup_timeout)));
        } catch (...) {
        }
        state_->release(std::move(lease_), healthy && !invalid_);
        return healthy;
    }

    std::shared_ptr<State> state_;
    ConnectionLease lease_;
    std::thread::id owner_;
    bool invalid_{false};
};

TransactionDatabase::TransactionDatabase(TransactionOptions options, ConnectionFactory factory)
    : state_(std::make_shared<State>()) {
    state_->options = options;
    state_->factory = std::move(factory);
}

TransactionDatabase::~TransactionDatabase() { close(); }

core::Result<std::unique_ptr<ITransaction>> TransactionDatabase::begin(
    const core::CallContext& ctx) {
    if (core::has_expired(ctx))
        return rejected(core::ErrorCode::DeadlineExceeded, "transaction deadline");
    auto state = state_;
    if (!state->factory || state->options.cleanup_timeout.count() <= 0)
        return rejected(core::ErrorCode::InvalidArgument, "invalid transaction options");
    if (state->closing.load())
        return rejected(core::ErrorCode::Unavailable, "database context closed");

    core::Result<ConnectionLease> borrowed;
    try {
        borrowed = state->factory(ctx);
    } catch (...) {
        return rejected(core::ErrorCode::Unavailable, "database connection borrow failed");
    }
    if (!core::is_ok(borrowed)) {
        if (state->closing.load())
            return rejected(core::ErrorCode::Unavailable, "database context closed");
        return core::error_of(borrowed);
    }
    auto lease = std::move(core::value_of(borrowed));
    if (!lease.connection || !lease.release)
        return rejected(core::ErrorCode::Unavailable, "invalid database connection lease");
    if (state->closing.load() || core::has_expired(ctx)) {
        state->release(std::move(lease), true);
        return rejected(state->closing.load() ? core::ErrorCode::Unavailable
                                              : core::ErrorCode::DeadlineExceeded,
                        "database borrow cancelled");
    }
    core::Status started;
    try {
        started = lease.connection->begin(ctx);
    } catch (...) {
        started = rejected(core::ErrorCode::Unavailable, "database begin exception");
    }
    if (!core::is_ok(started)) {
        state->release(std::move(lease), false);
        return core::error_of(started);
    }
    return std::unique_ptr<ITransaction>(new Transaction(std::move(state), std::move(lease)));
}

core::Result<Connection*> TransactionDatabase::connection(ITransaction& tx,
                                                          const core::CallContext& ctx) {
    auto* transaction = dynamic_cast<Transaction*>(&tx);
    if (!transaction || transaction->state_ != state_ ||
        transaction->owner_ != std::this_thread::get_id() || !transaction->lease_.connection ||
        transaction->invalid_) {
        return rejected(core::ErrorCode::InvalidArgument,
                        "inactive or foreign database transaction");
    }
    if (core::has_expired(ctx)) {
        return rejected(core::ErrorCode::DeadlineExceeded, "repository deadline");
    }
    return transaction->lease_.connection.get();
}

void TransactionDatabase::invalidate(ITransaction& tx) {
    auto* transaction = dynamic_cast<Transaction*>(&tx);
    if (transaction && transaction->state_ == state_ &&
        transaction->owner_ == std::this_thread::get_id()) {
        transaction->invalid_ = true;
    }
}

void TransactionDatabase::close() { state_->closing.store(true); }

}  // namespace ems::database::detail

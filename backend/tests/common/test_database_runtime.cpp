#include <common/database/i_database.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <ormpp/connection_pool.hpp>
#include <stdexcept>
#include <thread>
#include <vector>

#include "common/db/runtime/transaction_database.h"

namespace {
using namespace ems;
using database::detail::Connection;
using database::detail::ConnectionLease;
using database::detail::TransactionDatabase;
using database::detail::TransactionOptions;

struct Counters {
    std::atomic<int> created{0};
    std::atomic<int> destroyed{0};
    std::atomic<int> rollbacks{0};
    std::atomic<int> commits{0};
    bool fail_connect{false};
    bool throw_connect{false};
    int fail_connect_number{0};
    bool fail_commit{false};
    bool fail_rollback{false};
    bool fail_begin{false};
    bool throw_begin{false};
    bool throw_commit{false};
    bool throw_rollback{false};
};

template <int Tag>
class FakeConnection final : public Connection {
   public:
    inline static std::shared_ptr<Counters> configured;

    FakeConnection() : counters_(configured) { ++counters_->created; }

    ~FakeConnection() override { ++counters_->destroyed; }

    bool connect(const std::tuple<std::string, std::string, std::string, std::string,
                                  std::optional<int>, std::optional<int>>&) {
        if (counters_->throw_connect) throw std::runtime_error("connect exception");
        return !counters_->fail_connect && counters_->created != counters_->fail_connect_number;
    }

    bool ping() const { return true; }

    bool has_error() const { return false; }

    auto get_latest_operate_time() const { return std::chrono::system_clock::now(); }

    void update_operate_time() {}

    core::Status begin(const core::CallContext&) override {
        if (counters_->throw_begin) throw std::runtime_error("begin transport exception");
        if (counters_->fail_begin) return fail();
        return core::ok_status();
    }

    core::Status commit(const core::CallContext&) override {
        ++counters_->commits;
        if (counters_->throw_commit) throw std::runtime_error("commit transport exception");
        if (counters_->fail_commit) return fail();
        return core::ok_status();
    }

    core::Status rollback(const core::CallContext&) override {
        ++counters_->rollbacks;
        if (counters_->throw_rollback) throw std::runtime_error("rollback transport exception");
        if (counters_->fail_rollback) return fail();
        return core::ok_status();
    }

   private:
    core::Status fail() {
        return core::make_error(core::ErrorCode::Unavailable, core::WriteEffect::Unknown,
                                "lost response");
    }

    std::shared_ptr<Counters> counters_;
};

template <int Tag>
auto factory(const std::shared_ptr<Counters>& counters,
             std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(1000)) {
    using Driver = FakeConnection<Tag>;
    using Pool = ormpp::connection_pool<Driver>;
    Driver::configured = counters;
    Pool::instance().init(1);
    return [acquire_timeout](const core::CallContext& ctx) -> core::Result<ConnectionLease> {
        auto connection = Pool::instance().get_until(
            std::min(ctx.deadline, std::chrono::steady_clock::now() + acquire_timeout));
        if (!connection)
            return core::make_error(core::has_expired(ctx) ? core::ErrorCode::DeadlineExceeded
                                                           : core::ErrorCode::ResourceExhausted,
                                    core::WriteEffect::NotApplied, "test pool borrow failed");
        return ConnectionLease{
            std::move(connection), [](std::shared_ptr<Connection> borrowed, bool healthy) {
                Pool::instance().return_back(std::static_pointer_cast<Driver>(std::move(borrowed)),
                                             healthy);
            }};
    };
}

auto context() { return core::make_call_context(std::chrono::seconds(3)); }

std::unique_ptr<database::ITransaction> begin(TransactionDatabase& db) {
    auto result = db.begin(context());
    EXPECT_TRUE(core::is_ok(result));
    return core::is_ok(result) ? std::move(std::get<0>(result)) : nullptr;
}

TEST(DatabaseRuntime, RepositoriesShareOneConnectionAndTransaction) {
    auto counters = std::make_shared<Counters>();
    TransactionDatabase db({}, factory<1>(counters));
    auto tx = begin(db);
    auto first = db.connection(*tx, context());
    auto second = db.connection(*tx, context());
    ASSERT_TRUE(core::is_ok(first));
    ASSERT_TRUE(core::is_ok(second));
    EXPECT_EQ(core::value_of(first), core::value_of(second));
    EXPECT_TRUE(core::is_ok(tx->commit(context())));
    EXPECT_FALSE(core::is_ok(db.connection(*tx, context())));
    EXPECT_FALSE(core::is_ok(tx->commit(context())));
    EXPECT_TRUE(core::is_ok(tx->rollback(context())));
    auto next = begin(db);
    EXPECT_EQ(counters->created, 1);
}

TEST(DatabaseRuntime, AbandonedTransactionRollsBackBeforeConnectionReuse) {
    auto counters = std::make_shared<Counters>();
    TransactionDatabase db({}, factory<2>(counters));
    begin(db).reset();
    EXPECT_EQ(counters->rollbacks, 1);
    auto tx = begin(db);
    EXPECT_EQ(counters->created, 1);
    EXPECT_TRUE(core::is_ok(tx->rollback(context())));
    tx.reset();
    EXPECT_EQ(counters->rollbacks, 2);
}

TEST(DatabaseRuntime, CommitUnknownDiscardsConnectionAndCannotBeRetried) {
    auto counters = std::make_shared<Counters>();
    TransactionDatabase db({}, factory<3>(counters));
    auto tx = begin(db);
    counters->fail_commit = true;
    auto result = tx->commit(context());
    ASSERT_FALSE(core::is_ok(result));
    EXPECT_EQ(core::error_of(result).effect, core::WriteEffect::Unknown);
    EXPECT_EQ(counters->destroyed, 1);
    EXPECT_FALSE(core::is_ok(tx->commit(context())));
    EXPECT_EQ(counters->commits, 1);
    counters->fail_commit = false;
    auto replacement = begin(db);
    EXPECT_EQ(counters->created, 2);
}

TEST(DatabaseRuntime, RollbackFailureAndInvalidationDiscardConnection) {
    auto counters = std::make_shared<Counters>();
    TransactionDatabase db({}, factory<4>(counters));
    auto tx = begin(db);
    counters->fail_rollback = true;
    tx.reset();
    EXPECT_EQ(counters->destroyed, 1);
    counters->fail_rollback = false;
    tx = begin(db);
    db.invalidate(*tx);
    EXPECT_FALSE(core::is_ok(db.connection(*tx, context())));
    EXPECT_FALSE(core::is_ok(tx->commit(context())));
    EXPECT_EQ(counters->commits, 0);
    EXPECT_EQ(counters->destroyed, 2);
}

TEST(DatabaseRuntime, ForeignContextAndThreadCannotUseTransaction) {
    auto counters = std::make_shared<Counters>();
    TransactionDatabase first({}, factory<5>(counters)), second({}, factory<5>(counters));
    auto tx = begin(first);
    EXPECT_FALSE(core::is_ok(second.connection(*tx, context())));
    second.invalidate(*tx);
    std::thread wrong_thread([&] {
        EXPECT_FALSE(core::is_ok(first.connection(*tx, context())));
        EXPECT_FALSE(core::is_ok(tx->commit(context())));
        EXPECT_FALSE(core::is_ok(tx->rollback(context())));
    });
    wrong_thread.join();
    EXPECT_TRUE(core::is_ok(tx->commit(context())));
}

TEST(DatabaseRuntime, PoolExhaustionAndCallerDeadlineAreBounded) {
    auto counters = std::make_shared<Counters>();
    TransactionOptions options;
    TransactionDatabase db(options, factory<6>(counters, std::chrono::milliseconds(30)));
    auto tx = begin(db);
    auto exhausted = db.begin(context());
    ASSERT_FALSE(core::is_ok(exhausted));
    EXPECT_EQ(core::error_of(exhausted).code, core::ErrorCode::ResourceExhausted);
    auto expired = db.begin(core::make_call_context(std::chrono::milliseconds(5)));
    ASSERT_FALSE(core::is_ok(expired));
    EXPECT_EQ(core::error_of(expired).code, core::ErrorCode::DeadlineExceeded);
    EXPECT_EQ(counters->created, 1);
    tx.reset();
    auto next = begin(db);
    EXPECT_EQ(counters->created, 1);
}

TEST(DatabaseRuntime, ClosingContextPreservesLibraryPoolAndInFlightTransaction) {
    auto counters = std::make_shared<Counters>();
    TransactionOptions options;
    auto db = std::make_unique<TransactionDatabase>(options, factory<7>(counters));
    auto tx = begin(*db);
    std::promise<void> entered;
    auto waiting = std::async(std::launch::async, [&] {
        entered.set_value();
        return db->begin(context());
    });
    entered.get_future().wait();
    db->close();
    auto result = waiting.get();
    ASSERT_FALSE(core::is_ok(result));
    EXPECT_EQ(core::error_of(result).code, core::ErrorCode::Unavailable);
    db.reset();
    EXPECT_TRUE(core::is_ok(tx->rollback(context())));
    EXPECT_EQ(counters->destroyed, 0);
}

TEST(DatabaseRuntime, FailedFactoryOrBeginDoesNotLeakCapacity) {
    auto counters = std::make_shared<Counters>();
    bool throws = true;
    TransactionOptions options;
    auto source = factory<8>(counters);
    TransactionDatabase db(options,
                           [&](const core::CallContext& ctx) -> core::Result<ConnectionLease> {
                               if (throws) throw std::runtime_error("connect failed");
                               return source(ctx);
                           });
    EXPECT_FALSE(core::is_ok(db.begin(context())));
    throws = false;
    counters->fail_begin = true;
    EXPECT_FALSE(core::is_ok(db.begin(context())));
    counters->fail_begin = false;
    auto tx = begin(db);
    EXPECT_EQ(counters->created, 2);
}

TEST(DatabaseRuntime, ExpiredCommitRollsBackWithoutSendingCommit) {
    auto counters = std::make_shared<Counters>();
    TransactionDatabase db({}, factory<9>(counters));
    auto tx = begin(db);
    auto result = tx->commit(core::immediate_context());
    ASSERT_FALSE(core::is_ok(result));
    EXPECT_EQ(core::error_of(result).effect, core::WriteEffect::NotApplied);
    EXPECT_EQ(counters->commits, 0);
    EXPECT_EQ(counters->rollbacks, 1);
}

TEST(DatabaseRuntime, DriverExceptionsDoNotLeakConnectionsOrLoseUnknownCommit) {
    auto counters = std::make_shared<Counters>();
    TransactionOptions options;
    TransactionDatabase db(options, factory<10>(counters));
    counters->throw_begin = true;
    EXPECT_FALSE(core::is_ok(db.begin(context())));
    EXPECT_EQ(counters->destroyed, 1);
    counters->throw_begin = false;
    auto tx = begin(db);
    counters->throw_commit = true;
    const auto committed = tx->commit(context());
    ASSERT_FALSE(core::is_ok(committed));
    EXPECT_EQ(core::error_of(committed).effect, core::WriteEffect::Unknown);
    EXPECT_EQ(counters->destroyed, 2);
    counters->throw_commit = false;
    tx = begin(db);
    counters->throw_rollback = true;
    EXPECT_FALSE(core::is_ok(tx->rollback(context())));
    EXPECT_EQ(counters->destroyed, 3);
    counters->throw_rollback = false;
    tx = begin(db);
    EXPECT_TRUE(core::is_ok(tx->commit(context())));
    EXPECT_EQ(counters->created, 4);
}

TEST(DatabaseOrmPool, FailedInitializationDoesNotRetainPartialConnections) {
    using Driver = FakeConnection<101>;
    auto counters = std::make_shared<Counters>();
    Driver::configured = counters;
    auto& pool = ormpp::connection_pool<Driver>::instance();
    counters->fail_connect_number = 2;
    EXPECT_THROW(pool.init(2), std::invalid_argument);
    EXPECT_EQ(counters->created, 2);
    EXPECT_EQ(counters->destroyed, 2);
    counters->fail_connect_number = 0;
    pool.init(2);
    auto first = pool.get_until(context().deadline);
    auto second = pool.get_until(context().deadline);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(pool.get_until(core::immediate_context().deadline), nullptr);
    pool.return_back(std::move(first), true);
    pool.return_back(std::move(second), true);
    EXPECT_EQ(counters->created - counters->destroyed, 2);
}

TEST(DatabaseOrmPool, ReconnectFailureAndExceptionPreservePoolSlot) {
    auto counters = std::make_shared<Counters>();
    auto source = factory<102>(counters);
    TransactionDatabase db({}, source);
    auto tx = begin(db);
    db.invalidate(*tx);
    tx.reset();
    counters->fail_connect = true;
    EXPECT_FALSE(core::is_ok(db.begin(context())));
    counters->fail_connect = false;
    counters->throw_connect = true;
    EXPECT_FALSE(core::is_ok(db.begin(context())));
    counters->throw_connect = false;
    tx = begin(db);
    ASSERT_NE(tx, nullptr);
    EXPECT_EQ(counters->created - counters->destroyed, 1);
}

TEST(DatabaseOrmPool, SameProcessCallersReuseLibrarySingleton) {
    auto counters = std::make_shared<Counters>();
    auto first_source = factory<103>(counters);
    auto second_source = factory<103>(counters);
    EXPECT_EQ(counters->created, 1);
    TransactionDatabase first({}, first_source), second({}, second_source);
    auto tx = begin(first);
    const auto pointer = core::value_of(first.connection(*tx, context()));
    tx.reset();
    auto next = begin(second);
    EXPECT_EQ(core::value_of(second.connection(*next, context())), pointer);
    EXPECT_EQ(counters->created, 1);
}

TEST(DatabaseOrmPool, ConcurrentBorrowersNeverExceedConfiguredCapacity) {
    using Driver = FakeConnection<104>;
    auto counters = std::make_shared<Counters>();
    Driver::configured = counters;
    auto& pool = ormpp::connection_pool<Driver>::instance();
    pool.init(2);
    std::atomic<int> active{0};
    std::atomic<int> completed{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) {
        workers.emplace_back([&] {
            auto connection = pool.get_until(context().deadline);
            EXPECT_NE(connection, nullptr);
            if (!connection) return;
            EXPECT_LE(++active, 2);
            std::this_thread::yield();
            --active;
            pool.return_back(std::move(connection), true);
            ++completed;
        });
    }
    for (auto& worker : workers) worker.join();
    EXPECT_EQ(completed, 8);
    EXPECT_EQ(counters->created, 2);
}
}  // namespace

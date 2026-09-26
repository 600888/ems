#include <gtest/gtest.h>
#include <realtime/redis_store.h>
#include <realtime/shared_memory_store.h>

#include <atomic>
#include <cstdlib>
#include <future>
#include <thread>

#include "store_detail.h"
#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ems::realtime {
void set_shared_memory_test_hook(void (*)(int));

namespace {
CallContext ctx(int ms = 2000) { return core::make_call_context(std::chrono::milliseconds(ms)); }

void crash_exit(int code) {
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
#else
    _exit(code);
#endif
}

RecordKey key(std::string name = "active_power") {
    return {{ScopeKind::Unit, "ess01"}, RecordKind::Point, {"pcs01"}, std::move(name)};
}

template <class T>
T take(Result<T> value) {
    if (!core::is_ok(value)) throw std::runtime_error(core::error_of(value).message);
    return std::get<T>(std::move(value));
}

template <class T>
void expect_error(const Result<T>& value, ErrorCode code, WriteEffect effect) {
    ASSERT_FALSE(core::is_ok(value));
    EXPECT_EQ(core::error_of(value).code, code);
    EXPECT_EQ(core::error_of(value).effect, effect);
}

class BackendContract : public ::testing::TestWithParam<std::string> {
   protected:
    std::unique_ptr<IRealtimeStore> store, peer;
    SharedMemoryOptions shm;
    std::string scope;
    bool redis = false;

    void SetUp() override {
        scope = "t" + take(detail::token());
        redis = GetParam() == "redis";
        if (redis) {
#ifdef EMS_REALTIME_TEST_REDIS
            auto port = std::getenv("EMS_TEST_REDIS_PORT");
            if (!port) GTEST_SKIP() << "Set EMS_TEST_REDIS_PORT to an isolated test Redis primary";
            RedisOptions o;
            o.port = static_cast<unsigned>(std::stoul(port));
            o.max_record_bytes = 512;
            o.max_batch_items = 4;
            store = take(RedisStore::open(o, ctx()));
            peer = take(RedisStore::open(o, ctx()));
#endif
        } else {
#ifdef EMS_REALTIME_TEST_SHM
            shm.segment_name = scope;
            shm.create_if_missing = true;
            shm.capacity = 16;
            shm.shard_count = 2;
            shm.max_record_bytes = 512;
            shm.max_batch_items = 4;
            store = take(SharedMemoryStore::open(shm, ctx()));
            shm.create_if_missing = false;
            peer = take(SharedMemoryStore::open(shm, ctx()));
#endif
        }
    }

    RecordKey k(std::string name = "active_power") {
        auto r = key(name);
        r.scope.code = scope;
        return r;
    }

    void TearDown() override {
        // Delete only this test's known keys. Never FLUSHDB or touch another scope.
        if (redis && peer)
            for (auto n : {"active_power", "other", "third"}) {
                auto r = peer->read(k(n), ctx());
                if (core::is_ok(r)) peer->erase({k(n), core::value_of(r).revision}, ctx());
            }
        peer.reset();
        store.reset();
#ifndef _WIN32
        if (!redis && !shm.segment_name.empty())
            shm_unlink(("/ems_rt_" + shm.segment_name).c_str());
#endif
    }
};

TEST_P(BackendContract, CompleteReplacementCopiedReadAndABA) {
    expect_error(store->read(k(), ctx()), ErrorCode::NotFound, WriteEffect::NotApplicable);
    auto first = take(store->compareExchange(
        {k(), {{"value", "18446744073709551615"}, {"old", "remove"}}, {}}, ctx()));
    auto snapshot = take(peer->read(k(), ctx()));
    EXPECT_EQ(snapshot.fields.at("value"), "18446744073709551615");
    expect_error(store->compareExchange({k(), {}, {}}, ctx()), ErrorCode::Conflict,
                 WriteEffect::NotApplied);
    auto second = take(peer->compareExchange({k(), {{"value", "null"}}, first.revision}, ctx()));
    EXPECT_NE(first.revision, second.revision);
    EXPECT_EQ(take(store->read(k(), ctx())).fields.count("old"), 0u);
    EXPECT_EQ(snapshot.fields.at("old"), "remove");
    expect_error(store->erase({k(), first.revision}, ctx()), ErrorCode::Conflict,
                 WriteEffect::NotApplied);
    take(store->erase({k(), second.revision}, ctx()));
    expect_error(store->erase({k(), second.revision}, ctx()), ErrorCode::NotFound,
                 WriteEffect::NotApplied);
    auto third = take(store->compareExchange({k(), {}, {}}, ctx()));
    EXPECT_NE(third.revision, second.revision);
    expect_error(store->compareExchange({k(), {}, second.revision}, ctx()), ErrorCode::Conflict,
                 WriteEffect::NotApplied);
    EXPECT_TRUE(take(peer->read(k(), ctx())).fields.empty());
}

TEST_P(BackendContract, ConcurrentCASHasOneWinner) {
    auto initial = take(store->compareExchange({k(), {{"v", "initial"}}, {}}, ctx()));
    std::atomic<int> wins{0}, conflicts{0};
    std::promise<void> start;
    auto signal = start.get_future().share();
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
        threads.emplace_back([&, i] {
            signal.wait();
            auto r =
                (i % 2 ? store : peer)
                    ->compareExchange({k(), {{"v", std::to_string(i)}}, initial.revision}, ctx());
            if (core::is_ok(r))
                ++wins;
            else if (core::error_of(r).code == ErrorCode::Conflict)
                ++conflicts;
        });
    start.set_value();
    for (auto& t : threads) t.join();
    EXPECT_EQ(wins, 1);
    EXPECT_EQ(conflicts, 7);
}

TEST_P(BackendContract, InputLimitsDeadlineAndClose) {
    expect_error(store->compareExchange({k(), {{"_store_revision", "forged"}}, {}}, ctx()),
                 ErrorCode::InvalidArgument, WriteEffect::NotApplied);
    expect_error(store->compareExchange({k(), {{"v", std::string(600, 'x')}}, {}}, ctx()),
                 ErrorCode::ResourceExhausted, WriteEffect::NotApplied);
    expect_error(store->compareExchange({k(), {{"v", std::string(1, char(0xff))}}, {}}, ctx()),
                 ErrorCode::InvalidArgument, WriteEffect::NotApplied);
    expect_error(store->compareExchange({k(), {}, {}}, ctx(-1)), ErrorCode::DeadlineExceeded,
                 WriteEffect::NotApplied);
    expect_error(store->read(k(), ctx(-1)), ErrorCode::DeadlineExceeded,
                 WriteEffect::NotApplicable);
    auto bad = k();
    bad.device_path = {"rt"};
    expect_error(store->read(bad, ctx()), ErrorCode::InvalidArgument, WriteEffect::NotApplicable);
    auto binary = take(store->compareExchange({k(), {{"v", std::string("a\0b", 3)}}, {}}, ctx()));
    EXPECT_EQ(take(peer->read(k(), ctx())).fields.at("v"), std::string("a\0b", 3));
    store->close();
    store->close();
    expect_error(store->read(k(), ctx()), ErrorCode::NotReady, WriteEffect::NotApplicable);
    expect_error(store->compareExchange({k(), {}, binary.revision}, ctx()), ErrorCode::NotReady,
                 WriteEffect::NotApplied);
    EXPECT_EQ(take(peer->read(k(), ctx())).revision, binary.revision);
}

TEST_P(BackendContract, BatchOrderPartialFailureAndNoOversizeEffects) {
    std::vector<WriteRequest> batch{{k(), {{"v", "one"}}, {}}, {k(), {}, {}}, {k("other"), {}, {}}};
    auto results = store->writeMany(batch, ctx());
    ASSERT_EQ(results.size(), 3u);
    EXPECT_TRUE(core::is_ok(results[0]));
    expect_error(results[1], ErrorCode::Conflict, WriteEffect::NotApplied);
    EXPECT_TRUE(core::is_ok(results[2]));
    std::vector<WriteRequest> oversized(5, WriteRequest{k("third"), {}, {}});
    for (auto& r : store->writeMany(oversized, ctx()))
        expect_error(r, ErrorCode::InvalidArgument, WriteEffect::NotApplied);
    expect_error(peer->read(k("third"), ctx()), ErrorCode::NotFound, WriteEffect::NotApplicable);
    EXPECT_TRUE(store->readMany({}, ctx()).empty());
    EXPECT_TRUE(store->writeMany({}, ctx()).empty());
    auto reads = store->readMany({k("third"), k(), k()}, ctx());
    EXPECT_EQ(reads.size(), 3u);
    EXPECT_EQ(take(std::move(reads[1])).revision, take(std::move(reads[2])).revision);
}

std::vector<std::string> backends() {
    std::vector<std::string> out;
#ifdef EMS_REALTIME_TEST_SHM
    out.push_back("shm");
#endif
#ifdef EMS_REALTIME_TEST_REDIS
    out.push_back("redis");
#endif
    return out;
}

INSTANTIATE_TEST_SUITE_P(SharedContract, BackendContract, ::testing::ValuesIn(backends()));

TEST(KeyEncoding, PhysicalKeysAndInvalidIdentity) {
    EXPECT_EQ(detail::physical_key(key()), "ems:rt:point:{ess01}:pcs01:active_power");
    RecordKey station{{ScopeKind::Station, "station01"}, RecordKind::ChannelState, {}, "rs48501"};
    EXPECT_EQ(detail::physical_key(station), "ems:{station:station01}:channel:rs48501:rt:state");
    auto bad = key();
    bad.scope.code = "bad:{key}";
    EXPECT_FALSE(validate_record_key(bad).empty());
    bad = key();
    bad.kind = static_cast<RecordKind>(99);
    EXPECT_FALSE(validate_record_key(bad).empty());
    bad = key();
    bad.kind = RecordKind::AssetState;
    bad.name = "";
    bad.device_path.resize(9, "rack01");
    EXPECT_FALSE(validate_record_key(bad).empty());
}

#ifdef EMS_REALTIME_TEST_SHM
struct ShmTest : ::testing::Test {
    SharedMemoryOptions options;
    std::unique_ptr<IRealtimeStore> owner;

    void SetUp() override {
        // GTest re-executes the fixture on Windows: use the inherited run identity.
        options.segment_name =
            std::string("t") + std::getenv("EMS_TEST_RUN_ID") +
            std::to_string(
                detail::hash(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        options.capacity = 2;
        options.shard_count = 1;
        options.max_record_bytes = 128;
        options.create_if_missing = true;
        owner = take(SharedMemoryStore::open(options, ctx()));
    }

    WriteReceipt seed() {
        auto old = owner->read(key(), ctx());
        if (core::is_ok(old)) return {core::value_of(old).revision};
        return take(owner->compareExchange({key(), {{"v", "old"}}, {}}, ctx()));
    }

    void TearDown() override {
        owner.reset();
        set_shared_memory_test_hook(nullptr);
#ifndef _WIN32
        shm_unlink(("/ems_rt_" + options.segment_name).c_str());
#endif
    }
};

TEST_F(ShmTest, CapacityReuseAndAttachFences) {
    auto one = take(owner->compareExchange({key(), {}, {}}, ctx()));
    take(owner->compareExchange({key("other"), {}, {}}, ctx()));
    expect_error(owner->compareExchange({key("third"), {}, {}}, ctx()),
                 ErrorCode::ResourceExhausted, WriteEffect::NotApplied);
    take(owner->erase({key(), one.revision}, ctx()));
    take(owner->compareExchange({key("third"), {}, {}}, ctx()));
    auto mismatch = options;
    mismatch.max_record_bytes = 512;
    EXPECT_FALSE(core::is_ok(SharedMemoryStore::open(mismatch, ctx())));
    mismatch = options;
    mismatch.expected_generation = "wrong";
    expect_error(SharedMemoryStore::open(mismatch, ctx()), ErrorCode::NotReady,
                 WriteEffect::NotApplied);
    mismatch = options;
    mismatch.create_if_missing = false;
    mismatch.expected_generation = owner->info().storage_generation;
    EXPECT_TRUE(core::is_ok(SharedMemoryStore::open(mismatch, ctx())));
}

TEST_F(ShmTest, FactoryRejectsUnknownOptions) {
    BackendFactory factory;
    ASSERT_TRUE(core::is_ok(register_shared_memory_backend(factory)));
    factory.freeze();
    expect_error(
        factory.create({"shm", {{"segment_name", options.segment_name}, {"typo", "true"}}}, ctx()),
        ErrorCode::InvalidArgument, WriteEffect::NotApplied);
    expect_error(factory.create(
                     {"shm", {{"segment_name", options.segment_name}, {"capacity", "-1"}}}, ctx()),
                 ErrorCode::InvalidArgument, WriteEffect::NotApplied);
}

// Each subprocess reopens the OS mapping, proving actual cross-process access.
// Crash immediately before or after the aligned selector update while holding the shard lock.
TEST_F(ShmTest, OwnerDeathBeforeCommitPreservesOldImage) {
    auto initial = seed();
    ASSERT_EXIT(
        {
            auto other = take(SharedMemoryStore::open(options, ctx()));
            set_shared_memory_test_hook([](int stage) {
                if (stage == 1) crash_exit(42);
            });
            other->compareExchange({key(), {{"v", "new"}}, initial.revision}, ctx());
            crash_exit(1);
        },
        ::testing::ExitedWithCode(42), "");
    EXPECT_EQ(take(owner->read(key(), ctx())).fields.at("v"), "old");
    EXPECT_TRUE(
        core::is_ok(owner->compareExchange({key(), {{"v", "after"}}, initial.revision}, ctx())));
}

TEST_F(ShmTest, OwnerDeathAfterCommitPreservesNewImage) {
    auto initial = seed();
    ASSERT_EXIT(
        {
            auto other = take(SharedMemoryStore::open(options, ctx()));
            set_shared_memory_test_hook([](int stage) {
                if (stage == 2) crash_exit(42);
            });
            other->compareExchange({key(), {{"v", "new"}}, initial.revision}, ctx());
            crash_exit(1);
        },
        ::testing::ExitedWithCode(42), "");
    EXPECT_EQ(take(owner->read(key(), ctx())).fields.at("v"), "new");
    expect_error(owner->compareExchange({key(), {}, initial.revision}, ctx()), ErrorCode::Conflict,
                 WriteEffect::NotApplied);
}
#endif
}  // namespace
}  // namespace ems::realtime

int main(int argc, char** argv) {
    if (!std::getenv("EMS_TEST_RUN_ID")) {
        auto id = ems::realtime::detail::token();
        if (!ems::core::is_ok(id)) return 2;
        const auto value = ems::core::value_of(id).substr(0, 12);
#ifdef _WIN32
        _putenv_s("EMS_TEST_RUN_ID", value.c_str());
#else
        setenv("EMS_TEST_RUN_ID", value.c_str(), 1);
#endif
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

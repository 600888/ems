/**
 * @file test_realtime.cpp
 * @brief realtime API 测试：工厂注册/冻结、RecordKey 校验、批量默认实现语义
 */
#include <gtest/gtest.h>
#include <realtime/backend_factory.h>
#include <realtime/realtime_store.h>

#include <map>
#include <memory>
#include <string>

namespace ems::realtime {
namespace {

core::CallContext ctx_short() { return core::make_call_context(std::chrono::milliseconds(100)); }

/// 内存 Fake 后端：验证 CAS 语义（批量默认实现语义由基类保证）
class FakeStore final : public RealtimeStoreBase {
   public:
    BackendInfo info() const override {
        BackendInfo i;
        i.implementation = "fake";
        i.max_record_bytes = 4096;
        i.max_batch_items = 64;
        return i;
    }

    Result<Record> read(const RecordKey& key, const CallContext&) override {
        auto it = records_.find(key_of(key));
        if (it == records_.end()) {
            return core::make_error(ErrorCode::NotFound, WriteEffect::NotApplicable, "no record");
        }
        return it->second;
    }

    Result<WriteReceipt> compareExchange(const WriteRequest& req, const CallContext&) override {
        const std::string k = key_of(req.key);
        auto it = records_.find(k);
        if (!req.expected.has_value()) {
            // 仅在不存在时创建
            if (it != records_.end()) {
                return core::make_error(ErrorCode::Conflict, WriteEffect::NotApplied, "exists");
            }
            WriteReceipt r;
            r.revision.opaque = "rev-" + std::to_string(++counter_);
            records_.emplace(k, Record{req.replacement, r.revision});
            return r;
        }
        if (it == records_.end()) {
            return core::make_error(ErrorCode::NotFound, WriteEffect::NotApplied, "missing");
        }
        if (!(it->second.revision == *req.expected)) {
            return core::make_error(ErrorCode::Conflict, WriteEffect::NotApplied, "cas mismatch");
        }
        WriteReceipt r;
        r.revision.opaque = "rev-" + std::to_string(++counter_);
        it->second = Record{req.replacement, r.revision};
        return r;
    }

    Result<EraseReceipt> erase(const EraseRequest& req, const CallContext&) override {
        const std::string k = key_of(req.key);
        auto it = records_.find(k);
        if (it == records_.end()) {
            return core::make_error(ErrorCode::NotFound, WriteEffect::NotApplied, "missing");
        }
        if (!(it->second.revision == req.expected)) {
            return core::make_error(ErrorCode::Conflict, WriteEffect::NotApplied, "cas mismatch");
        }
        records_.erase(it);
        return EraseReceipt{};
    }

    void close() noexcept override { closed = true; }

    bool closed{false};

   private:
    static std::string key_of(const RecordKey& k) {
        std::string s = (k.scope.kind == ScopeKind::Station ? "S:" : "U:") + k.scope.code;
        for (const auto& p : k.device_path) s += "/" + p;
        s += "|" + std::to_string(static_cast<int>(k.kind)) + "|" + k.name;
        return s;
    }
    std::map<std::string, Record> records_;
    int counter_{0};
};

RecordKey point_key(const std::string& code) {
    RecordKey k;
    k.scope = Scope{ScopeKind::Unit, "u1"};
    k.kind = RecordKind::Point;
    k.device_path = {"pcs01"};
    k.name = code;
    return k;
}

// ---------------------------------------------------------------------------
// RecordKey 公共验证器
// ---------------------------------------------------------------------------

TEST(RealtimeKey, ChannelStateRequiresStationScope) {
    RecordKey k;
    k.scope = Scope{ScopeKind::Unit, "u1"};
    k.kind = RecordKind::ChannelState;
    k.name = "ch01";
    EXPECT_FALSE(validate_record_key(k).empty());

    k.scope = Scope{ScopeKind::Station, "st1"};
    EXPECT_TRUE(validate_record_key(k).empty()) << validate_record_key(k);

    k.device_path = {"pcs01"};  // 路径必须为空
    EXPECT_FALSE(validate_record_key(k).empty());
}

TEST(RealtimeKey, CommandStateRequiresEmptyPathAndId) {
    RecordKey k;
    k.scope = Scope{ScopeKind::Unit, "u1"};
    k.kind = RecordKind::CommandState;
    EXPECT_FALSE(validate_record_key(k).empty());  // 缺命令 ID
    k.name = "cmd-1";
    EXPECT_TRUE(validate_record_key(k).empty());
    k.device_path = {"pcs01"};
    EXPECT_FALSE(validate_record_key(k).empty());
}

TEST(RealtimeKey, AssetStateForbidsName) {
    RecordKey k;
    k.scope = Scope{ScopeKind::Unit, "u1"};
    k.kind = RecordKind::AssetState;
    EXPECT_TRUE(validate_record_key(k).empty());
    k.name = "x";
    EXPECT_FALSE(validate_record_key(k).empty());
}

TEST(RealtimeKey, PointRequiresCode) {
    RecordKey k = point_key("");
    EXPECT_FALSE(validate_record_key(k).empty());
    k.name = "active_power";
    EXPECT_TRUE(validate_record_key(k).empty());
}

// ---------------------------------------------------------------------------
// 批量默认实现（RealtimeStoreBase）
// ---------------------------------------------------------------------------

TEST(RealtimeBatch, ReadManyResultsMatchInput) {
    FakeStore store;
    WriteRequest create;
    create.key = point_key("p1");
    create.replacement["value"] = "100";
    ASSERT_TRUE(core::is_ok(store.compareExchange(create, ctx_short())));

    auto results = store.readMany({point_key("p1"), point_key("p2"), point_key("p1")}, ctx_short());
    ASSERT_EQ(results.size(), 3u);
    EXPECT_TRUE(core::is_ok(results[0]));
    EXPECT_FALSE(core::is_ok(results[1]));  // NotFound，不伪装 0
    EXPECT_EQ(core::error_of(results[1]).code, ErrorCode::NotFound);
    EXPECT_TRUE(core::is_ok(results[2]));
}

TEST(RealtimeBatch, OverLimitReturnsPerItemInvalidArgument) {
    FakeStore store;
    std::vector<RecordKey> keys(65);  // > max_batch_items(64)
    auto results = store.readMany(keys, ctx_short());
    ASSERT_EQ(results.size(), 65u);
    for (const auto& r : results) {
        ASSERT_FALSE(core::is_ok(r));
        EXPECT_EQ(core::error_of(r).code, ErrorCode::InvalidArgument);
    }
    // 读批量 effect = NotApplicable
    EXPECT_EQ(core::error_of(results[0]).effect, WriteEffect::NotApplicable);
}

TEST(RealtimeBatch, WriteManyOverLimitUsesNotApplied) {
    FakeStore store;
    std::vector<WriteRequest> reqs(65);
    auto results = store.writeMany(reqs, ctx_short());
    ASSERT_EQ(results.size(), 65u);
    EXPECT_EQ(core::error_of(results[0]).effect, WriteEffect::NotApplied);
}

TEST(RealtimeBatch, SameKeyOrderPreserved) {
    FakeStore store;
    WriteRequest create;
    create.key = point_key("p1");
    create.replacement["v"] = "1";
    auto r0 = store.compareExchange(create, ctx_short());
    ASSERT_TRUE(core::is_ok(r0));

    // 两个请求同键同 expected：后项可以看到前项效果 —— 第二个应 Conflict
    WriteRequest w1;
    w1.key = point_key("p1");
    w1.replacement["v"] = "2";
    w1.expected = core::value_of(r0).revision;

    WriteRequest w2 = w1;  // 相同 expected
    w2.replacement["v"] = "3";

    auto results = store.writeMany({w1, w2}, ctx_short());
    ASSERT_EQ(results.size(), 2u);
    EXPECT_TRUE(core::is_ok(results[0]));
    EXPECT_FALSE(core::is_ok(results[1]));
    EXPECT_EQ(core::error_of(results[1]).code, ErrorCode::Conflict);
}

// ---------------------------------------------------------------------------
// CAS 基础语义
// ---------------------------------------------------------------------------

TEST(RealtimeCas, CreateOnlyWhenAbsent) {
    FakeStore store;
    WriteRequest create;
    create.key = point_key("p1");
    create.replacement["v"] = "1";
    EXPECT_TRUE(core::is_ok(store.compareExchange(create, ctx_short())));
    // 再次仅创建 → Conflict
    auto again = store.compareExchange(create, ctx_short());
    EXPECT_EQ(core::error_of(again).code, ErrorCode::Conflict);
}

TEST(RealtimeCas, ConditionalReplaceAndErase) {
    FakeStore store;
    WriteRequest create;
    create.key = point_key("p1");
    create.replacement["v"] = "1";
    auto r0 = store.compareExchange(create, ctx_short());
    ASSERT_TRUE(core::is_ok(r0));

    const Revision wrong{"nope"};
    WriteRequest bad = create;
    bad.expected = wrong;
    EXPECT_EQ(core::error_of(store.compareExchange(bad, ctx_short())).code, ErrorCode::Conflict);

    EraseRequest del;
    del.key = point_key("p1");
    del.expected = wrong;
    EXPECT_EQ(core::error_of(store.erase(del, ctx_short())).code, ErrorCode::Conflict);
    del.expected = core::value_of(r0).revision;
    EXPECT_TRUE(core::is_ok(store.erase(del, ctx_short())));
    // 已删除：NotFound/NotApplied
    auto gone = store.erase(del, ctx_short());
    EXPECT_EQ(core::error_of(gone).code, ErrorCode::NotFound);
    EXPECT_EQ(core::error_of(gone).effect, WriteEffect::NotApplied);
}

// ---------------------------------------------------------------------------
// BackendFactory：注册/冻结/未知类型
// ---------------------------------------------------------------------------

TEST(RealtimeFactory, RegisterFreezeCreate) {
    BackendFactory factory;
    auto reg = factory.register_backend(
        "fake",
        [](const BackendSpec&, const CallContext&) -> Result<std::unique_ptr<IRealtimeStore>> {
            return std::unique_ptr<IRealtimeStore>(std::make_unique<FakeStore>());
        });
    ASSERT_TRUE(core::is_ok(reg));
    // 重复注册冲突
    auto dup = factory.register_backend(
        "fake",
        [](const BackendSpec&, const CallContext&) -> Result<std::unique_ptr<IRealtimeStore>> {
            return std::unique_ptr<IRealtimeStore>(std::make_unique<FakeStore>());
        });
    EXPECT_EQ(core::error_of(dup).code, ErrorCode::Conflict);
    factory.freeze();
    auto after = factory.register_backend(
        "late",
        [](const BackendSpec&, const CallContext&) -> Result<std::unique_ptr<IRealtimeStore>> {
            return std::unique_ptr<IRealtimeStore>(std::make_unique<FakeStore>());
        });
    EXPECT_EQ(core::error_of(after).code, ErrorCode::Conflict);

    BackendSpec spec;
    spec.type = "fake";
    auto created = factory.create(spec, ctx_short());
    ASSERT_TRUE(core::is_ok(created));
    EXPECT_EQ(core::value_of(created)->info().implementation, "fake");

    // 未编译的后端被配置选中：Unsupported，不悄悄替换
    BackendSpec unknown;
    unknown.type = "shm";
    auto missing = factory.create(unknown, ctx_short());
    ASSERT_FALSE(core::is_ok(missing));
    EXPECT_EQ(core::error_of(missing).code, ErrorCode::Unsupported);
}

}  // namespace
}  // namespace ems::realtime

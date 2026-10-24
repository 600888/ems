#include <rtdb/backend/redis_store.h>
#include <sw/redis++/redis++.h>

#include <atomic>
#include <iterator>
#include <limits>
#include <set>

#include "store_detail.h"

namespace ems::rtdb {
namespace {
using namespace detail;
namespace redis = sw::redis;

std::string server_generation(const std::string& info) {
    auto begin = info.find("run_id:");
    if (begin == std::string::npos) return {};
    begin += 7;
    auto end = info.find("\r\n", begin);
    if (end == std::string::npos || end == begin || end - begin > 128) return {};
    return info.substr(begin, end - begin);
}

Error reply_error(const redis::ReplyError& e, WriteEffect effect) {
    const std::string_view message(e.what());
    if (message.find("NOAUTH") == 0 || message.find("WRONGPASS") == 0 ||
        message.find("NOPERM") == 0)
        return error(ErrorCode::PermissionDenied, WriteEffect::NotApplied,
                     "Redis authorization denied");
    if (message.find("WRONGTYPE") == 0)
        return error(ErrorCode::SchemaMismatch, effect, "Redis key has incompatible type");
    if (message.find("MOVED") == 0 || message.find("ASK") == 0)
        return error(ErrorCode::Unsupported, effect, "Redis Cluster is not enabled");
    return error(ErrorCode::Unavailable, effect, "redis-plus-plus command rejected");
}

// Construct an expiring temporary hash, then use RENAME as the ONLY commit point.
// pcall cleanup covers runtime errors before publication. No operation follows a successful RENAME.
const char* cas_script = R"lua(
local kind=redis.call('TYPE',KEYS[1]).ok
if kind~='none' and kind~='hash' then return 4 end
local old=false
if kind=='hash' then
  old=redis.call('HGET',KEYS[1],'_store_revision')
  if not old or #old==0 or #old>128 then return 4 end
end
if ARGV[1]=='' then if old then return 2 end
else if not old then return 1 end; if old~=ARGV[1] then return 2 end end
if redis.call('EXISTS',KEYS[2])~=0 then return 2 end
local built=redis.pcall('HSET',KEYS[2],'_store_revision',ARGV[2])
if type(built)=='table' and built.err then return 5 end
local ttl=redis.pcall('PEXPIRE',KEYS[2],60000)
if type(ttl)=='table' and ttl.err then redis.pcall('DEL',KEYS[2]);return 5 end
for i=3,#ARGV,2 do
  local item=redis.pcall('HSET',KEYS[2],ARGV[i],ARGV[i+1])
  if type(item)=='table' and item.err then redis.pcall('DEL',KEYS[2]);return 5 end
end
local persisted=redis.pcall('PERSIST',KEYS[2])
if type(persisted)=='table' and persisted.err then redis.pcall('DEL',KEYS[2]);return 5 end
local published=redis.pcall('RENAME',KEYS[2],KEYS[1])
if type(published)=='table' and published.err then redis.pcall('DEL',KEYS[2]);return 5 end
return 0
)lua";
const char* erase_script = R"lua(
local kind=redis.call('TYPE',KEYS[1]).ok
if kind=='none' then return 1 end
if kind~='hash' then return 4 end
local old=redis.call('HGET',KEYS[1],'_store_revision')
if not old or #old==0 or #old>128 then return 4 end
if old~=ARGV[1] then return 2 end
redis.call('DEL',KEYS[1])
return 0
)lua";

Result<bool> script_result(long long status) {
    switch (status) {
        case 0:
            return true;
        case 1:
            return error(ErrorCode::NotFound, WriteEffect::NotApplied, "record not found");
        case 2:
            return error(ErrorCode::Conflict, WriteEffect::NotApplied, "revision conflict");
        case 4:
            return error(ErrorCode::SchemaMismatch, WriteEffect::NotApplied,
                         "Redis record type/revision incompatible");
        case 5:
            return error(ErrorCode::Unavailable, WriteEffect::NotApplied,
                         "Redis temporary publication failed");
        default:
            return error(ErrorCode::CorruptData, WriteEffect::Unknown,
                         "unknown Redis script result");
    }
}
}  // namespace

struct RedisStore::Impl {
    RedisOptions options;
    BackendInfo capabilities;
    std::unique_ptr<redis::Redis> client;
    std::atomic<bool> recovery_required{false};

    template <class T, class Operation>
    Result<T> run(const CallContext& ctx, bool write, Operation operation) {
        const auto effect = write ? WriteEffect::NotApplied : WriteEffect::NotApplicable;
        if (!client || recovery_required)
            return error(ErrorCode::NotReady, effect, "Redis store closed or recovery required");
        if (core::has_expired(ctx))
            return error(ErrorCode::DeadlineExceeded, effect, "Redis call budget exhausted");
        bool dispatched = false;
        try {
            if (server_generation(client->info("server")) != capabilities.storage_generation) {
                recovery_required = true;
                return error(ErrorCode::NotReady, effect,
                             "Redis generation changed; reauthorization required");
            }
            if (core::has_expired(ctx))
                return error(ErrorCode::DeadlineExceeded, effect, "Redis call budget exhausted");
            dispatched = true;
            return operation(*client);
        } catch (const redis::ReplyError& e) {
            return reply_error(e, write && dispatched ? WriteEffect::Unknown : effect);
        } catch (const redis::Error&) {
            recovery_required = true;
            return error(
                core::has_expired(ctx) ? ErrorCode::DeadlineExceeded : ErrorCode::Unavailable,
                write && dispatched ? WriteEffect::Unknown : effect,
                "redis-plus-plus transport/pool failure; reopen required");
        }
    }
};

RedisStore::RedisStore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

RedisStore::~RedisStore() = default;

BackendInfo RedisStore::info() const { return impl_->capabilities; }

void RedisStore::close() noexcept { impl_->client.reset(); }

Result<std::unique_ptr<IRealtimeStore>> RedisStore::open(const RedisOptions& o,
                                                         const CallContext& ctx) {
    constexpr auto effect = WriteEffect::NotApplied;
    if (core::has_expired(ctx))
        return error(ErrorCode::DeadlineExceeded, effect, "Redis initialization budget exhausted");
    if (o.address.empty() || !o.port || o.port > 65535 || o.database > 2147483647u ||
        !o.pool_size || o.pool_size > 64 || o.max_record_bytes < 4 ||
        o.max_record_bytes > 1048576 || !o.max_batch_items || o.max_batch_items > 65536 ||
        !o.transport_timeout_ms || o.transport_timeout_ms > 60000 || !o.pool_wait_timeout_ms ||
        o.pool_wait_timeout_ms > 60000 || (!o.username.empty() && o.password.empty()))
        return error(ErrorCode::InvalidArgument, effect, "invalid Redis options");
    try {
        auto p = std::make_unique<Impl>();
        p->options = o;
        redis::ConnectionOptions connection;
        connection.host = o.address;
        connection.port = static_cast<int>(o.port);
        connection.db = static_cast<int>(o.database);
        if (!o.username.empty()) connection.user = o.username;
        connection.password = o.password;
        connection.connect_timeout = std::chrono::milliseconds(o.transport_timeout_ms);
        connection.socket_timeout = std::chrono::milliseconds(o.transport_timeout_ms);
        redis::ConnectionPoolOptions pool;
        pool.size = o.pool_size;
        pool.wait_timeout = std::chrono::milliseconds(o.pool_wait_timeout_ms);
        p->client = std::make_unique<redis::Redis>(connection, pool);
        const auto generation = server_generation(p->client->info("server"));
        if (generation.empty())
            return error(ErrorCode::SchemaMismatch, effect, "Redis identity unavailable");
        if (p->client->info("replication").find("role:master\r\n") == std::string::npos)
            return error(ErrorCode::NotReady, effect, "Redis endpoint is not a writable primary");
        p->capabilities = {"redis",          generation, true, true, false, o.max_record_bytes,
                           o.max_batch_items};
        if (core::has_expired(ctx))
            return error(ErrorCode::DeadlineExceeded, effect,
                         "Redis initialization exceeded budget");
        return std::unique_ptr<IRealtimeStore>(new RedisStore(std::move(p)));
    } catch (const redis::ReplyError& e) {
        return reply_error(e, effect);
    } catch (const redis::Error&) {
        return error(core::has_expired(ctx) ? ErrorCode::DeadlineExceeded : ErrorCode::Unavailable,
                     effect, "redis-plus-plus initialization failed");
    }
}

Result<Record> RedisStore::read(const RecordKey& key, const CallContext& ctx) {
    if (!validate_record_key(key).empty())
        return error(ErrorCode::InvalidArgument, WriteEffect::NotApplicable, "invalid record key");
    return impl_->run<Record>(ctx, false, [&](redis::Redis& client) -> Result<Record> {
        Fields hash;
        client.hgetall(physical_key(key), std::inserter(hash, hash.end()));
        if (hash.empty())
            return error(ErrorCode::NotFound, WriteEffect::NotApplicable, "record not found");
        auto revision = hash.find("_store_revision");
        if (revision == hash.end() || revision->second.empty() || revision->second.size() > 128)
            return error(ErrorCode::SchemaMismatch, WriteEffect::NotApplicable,
                         "Redis revision missing");
        Record out;
        out.revision.opaque = std::move(revision->second);
        hash.erase(revision);
        if (!core::is_ok(
                encode_fields(hash, impl_->options.max_record_bytes, WriteEffect::NotApplicable)))
            return error(ErrorCode::CorruptData, WriteEffect::NotApplicable,
                         "Redis fields violate contract");
        out.fields = std::move(hash);
        return out;
    });
}

Result<std::vector<RecordKey>> RedisStore::listAssets(const Scope& scope, const CallContext& ctx) {
    constexpr auto effect = WriteEffect::NotApplicable;
    if (!validate_record_key({scope, RecordKind::AssetState, {}, {}}).empty())
        return error(ErrorCode::InvalidArgument, effect, "invalid scope");
    return impl_->run<std::vector<RecordKey>>(
        ctx, false, [&](redis::Redis& client) -> Result<std::vector<RecordKey>> {
            const auto pattern =
                "ems:" + hash_tag({scope, RecordKind::AssetState, {}, {}}) + "*:rt:state";
            std::set<std::string> seen;
            std::vector<RecordKey> out;
            long long cursor = 0;
            do {
                if (core::has_expired(ctx))
                    return error(ErrorCode::DeadlineExceeded, effect,
                                 "enumeration deadline exceeded");
                std::vector<std::string> page;
                cursor = client.scan(cursor, pattern, 128, std::back_inserter(page));
                for (const auto& physical : page) {
                    auto key = asset_key(scope, physical);
                    if (!key || !seen.insert(physical).second) continue;
                    if (out.size() == 65536)
                        return error(ErrorCode::ResourceExhausted, effect,
                                     "enumeration limit exceeded");
                    out.push_back(std::move(*key));
                }
            } while (cursor != 0);
            return out;
        });
}

Result<WriteReceipt> RedisStore::compareExchange(const WriteRequest& req, const CallContext& ctx) {
    if (!validate_record_key(req.key).empty() ||
        (req.expected && (req.expected->empty() || req.expected->opaque.size() > 128)))
        return error(ErrorCode::InvalidArgument, WriteEffect::NotApplied, "invalid key/revision");
    auto encoded =
        encode_fields(req.replacement, impl_->options.max_record_bytes, WriteEffect::NotApplied);
    if (!core::is_ok(encoded)) return core::error_of(encoded);
    auto nonce = token();
    if (!core::is_ok(nonce)) return core::error_of(nonce);
    const auto revision = impl_->capabilities.storage_generation + ":" + core::value_of(nonce);
    const std::vector<std::string> keys{
        physical_key(req.key), "ems:" + hash_tag(req.key) + ":_store_tmp:" + core::value_of(nonce)};
    std::vector<std::string> args{req.expected ? req.expected->opaque : "", revision};
    for (const auto& field : req.replacement) {
        args.push_back(field.first);
        args.push_back(field.second);
    }
    return impl_->run<WriteReceipt>(ctx, true, [&](redis::Redis& client) -> Result<WriteReceipt> {
        auto status = script_result(
            client.eval<long long>(cas_script, keys.begin(), keys.end(), args.begin(), args.end()));
        if (!core::is_ok(status)) return core::error_of(status);
        return WriteReceipt{Revision{revision}};
    });
}

Result<EraseReceipt> RedisStore::erase(const EraseRequest& req, const CallContext& ctx) {
    if (!validate_record_key(req.key).empty() || req.expected.empty() ||
        req.expected.opaque.size() > 128)
        return error(ErrorCode::InvalidArgument, WriteEffect::NotApplied, "invalid key/revision");
    return impl_->run<EraseReceipt>(ctx, true, [&](redis::Redis& client) -> Result<EraseReceipt> {
        const std::vector<std::string> keys{physical_key(req.key)}, args{req.expected.opaque};
        auto status = script_result(client.eval<long long>(erase_script, keys.begin(), keys.end(),
                                                           args.begin(), args.end()));
        if (!core::is_ok(status)) return core::error_of(status);
        return EraseReceipt{};
    });
}

Result<bool> register_redis_backend(BackendFactory& factory) {
    return factory.register_backend(
        "redis",
        [](const BackendSpec& spec,
           const CallContext& ctx) -> Result<std::unique_ptr<IRealtimeStore>> {
            if (!known_options(spec, {"address", "port", "database", "username", "password",
                                      "pool_size", "max_record_bytes", "max_batch_items",
                                      "transport_timeout_ms", "pool_wait_timeout_ms"}))
                return error(ErrorCode::InvalidArgument, WriteEffect::NotApplied,
                             "unknown/unsupported Redis option (standalone only)");
            RedisOptions o;
            for (const auto& kv : spec.options) {
                if (kv.first == "address")
                    o.address = kv.second;
                else if (kv.first == "username")
                    o.username = kv.second;
                else if (kv.first == "password")
                    o.password = kv.second;
                else {
                    std::size_t n;
                    if (!parse_size(kv.second, n) || n > std::numeric_limits<unsigned>::max())
                        return error(ErrorCode::InvalidArgument, WriteEffect::NotApplied,
                                     "invalid Redis numeric option");
                    if (kv.first == "port")
                        o.port = static_cast<unsigned>(n);
                    else if (kv.first == "database")
                        o.database = static_cast<unsigned>(n);
                    else if (kv.first == "pool_size")
                        o.pool_size = n;
                    else if (kv.first == "transport_timeout_ms")
                        o.transport_timeout_ms = n;
                    else if (kv.first == "pool_wait_timeout_ms")
                        o.pool_wait_timeout_ms = n;
                    else if (kv.first == "max_record_bytes")
                        o.max_record_bytes = n;
                    else
                        o.max_batch_items = n;
                }
            }
            return RedisStore::open(o, ctx);
        });
}
}  // namespace ems::rtdb

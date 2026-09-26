#include <hiredis.h>
#include <realtime/redis_store.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>

#include "store_detail.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace ems::realtime {
namespace {
using namespace detail;
using Reply = std::unique_ptr<redisReply, decltype(&freeReplyObject)>;
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid_socket = INVALID_SOCKET;

void close_socket(Socket s) { closesocket(s); }

bool again() {
    auto e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEINTR;
}

bool socket_runtime() {
    static const bool ready = []() {
        WSADATA d;
        return WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }();
    return ready;
}
#else
using Socket = int;
constexpr Socket invalid_socket = -1;

void close_socket(Socket s) { ::close(s); }

bool again() {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR;
}

bool socket_runtime() { return true; }
#endif
Error network_error(const CallContext& ctx, WriteEffect effect) {
    return error(core::has_expired(ctx) ? ErrorCode::DeadlineExceeded : ErrorCode::Unavailable,
                 effect, "Redis transport failed; connection must be revalidated");
}

bool wait_socket(Socket s, bool write, const CallContext& ctx) {
    while (!core::has_expired(ctx)) {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(core::remaining_budget(ctx))
                      .count();
        if (us <= 0) return false;
        timeval tv{static_cast<long>(std::min<std::int64_t>(us / 1000000, 3600)),
                   static_cast<long>(us % 1000000)};
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(s, &fds);
        auto n = select(static_cast<int>(s + 1), write ? nullptr : &fds, write ? &fds : nullptr,
                        nullptr, &tv);
        if (n > 0) return true;
        if (n < 0 && !again()) return false;
    }
    return false;
}

struct Connection {
    Socket socket = invalid_socket;

    ~Connection() {
        if (socket != invalid_socket) close_socket(socket);
    }

    Result<bool> connect(const RedisOptions& o, const CallContext& ctx) {
        if (!socket_runtime())
            return error(ErrorCode::Unavailable, WriteEffect::NotApplied,
                         "socket initialization failed");
        sockaddr_storage storage{};
        auto* v4 = reinterpret_cast<sockaddr_in*>(&storage);
        auto* v6 = reinterpret_cast<sockaddr_in6*>(&storage);
        int family = AF_INET;
        int length = sizeof(sockaddr_in);
        if (inet_pton(AF_INET, o.address.c_str(), &v4->sin_addr) == 1) {
            v4->sin_family = AF_INET;
            v4->sin_port = htons(static_cast<unsigned short>(o.port));
        } else if (inet_pton(AF_INET6, o.address.c_str(), &v6->sin6_addr) == 1) {
            family = AF_INET6;
            length = sizeof(sockaddr_in6);
            v6->sin6_family = AF_INET6;
            v6->sin6_port = htons(static_cast<unsigned short>(o.port));
        } else
            return error(ErrorCode::InvalidArgument, WriteEffect::NotApplied,
                         "Redis address must be numeric IPv4/IPv6");
        socket = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
        if (socket == invalid_socket) return network_error(ctx, WriteEffect::NotApplied);
#ifdef _WIN32
        u_long nonblocking = 1;
        if (ioctlsocket(socket, FIONBIO, &nonblocking) != 0)
            return network_error(ctx, WriteEffect::NotApplied);
#else
        if (socket >= FD_SETSIZE)
            return error(ErrorCode::ResourceExhausted, WriteEffect::NotApplied,
                         "socket exceeds select capacity");
        if (fcntl(socket, F_SETFL, O_NONBLOCK) < 0)
            return network_error(ctx, WriteEffect::NotApplied);
#endif
        if (::connect(socket, reinterpret_cast<sockaddr*>(&storage), length) != 0) {
            if (!again() || !wait_socket(socket, true, ctx))
                return network_error(ctx, WriteEffect::NotApplied);
            int status = 0;
#ifdef _WIN32
            int size = sizeof(status);
            if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&status), &size) !=
                    0 ||
                status != 0)
#else
            socklen_t size = sizeof(status);
            if (getsockopt(socket, SOL_SOCKET, SO_ERROR, &status, &size) != 0 || status != 0)
#endif
                return network_error(ctx, WriteEffect::NotApplied);
        }
        return true;
    }

    Result<Reply> command(const std::vector<std::string>& args, const CallContext& ctx,
                          bool mutating, std::size_t max_reply) {
        const auto before = mutating ? WriteEffect::NotApplied : WriteEffect::NotApplicable;
        if (core::has_expired(ctx)) return network_error(ctx, before);
        std::vector<const char*> argv;
        std::vector<std::size_t> sizes;
        for (const auto& arg : args) {
            argv.push_back(arg.data());
            sizes.push_back(arg.size());
        }
        char* raw = nullptr;
        auto length =
            redisFormatCommandArgv(&raw, static_cast<int>(argv.size()), argv.data(), sizes.data());
        std::unique_ptr<char, decltype(&redisFreeCommand)> buffer(raw, &redisFreeCommand);
        if (length < 0) return error(ErrorCode::Internal, before, "Redis command encoding failed");
        std::size_t sent = 0;
        while (sent < static_cast<std::size_t>(length)) {
            auto effect = mutating && sent ? WriteEffect::Unknown : before;
            if (!wait_socket(socket, true, ctx)) return network_error(ctx, effect);
#ifdef _WIN32
            auto n = send(socket, raw + sent, static_cast<int>(length - sent), 0);
#else
            auto n =
                send(socket, raw + sent, static_cast<std::size_t>(length) - sent, MSG_NOSIGNAL);
#endif
            if (n > 0)
                sent += static_cast<std::size_t>(n);
            else if (n == 0 || !again())
                return network_error(ctx, effect);
        }
        const auto after = mutating ? WriteEffect::Unknown : WriteEffect::NotApplicable;
        std::unique_ptr<redisReader, decltype(&redisReaderFree)> reader(redisReaderCreate(),
                                                                        &redisReaderFree);
        if (!reader) return error(ErrorCode::Internal, after, "Redis reader allocation failed");
        reader->maxelements = 2048;
        std::size_t received = 0;
        while (true) {
            if (!wait_socket(socket, false, ctx)) return network_error(ctx, after);
            char chunk[4096];
            auto n = recv(socket, chunk, sizeof(chunk), 0);
            if (n <= 0) {
                if (n < 0 && again()) continue;
                return network_error(ctx, after);
            }
            received += static_cast<std::size_t>(n);
            if (received > max_reply)
                return error(ErrorCode::CorruptData, after, "Redis reply exceeds configured bound");
            if (redisReaderFeed(reader.get(), chunk, static_cast<std::size_t>(n)) != REDIS_OK)
                return error(ErrorCode::CorruptData, after, "invalid Redis reply");
            void* object = nullptr;
            if (redisReaderGetReply(reader.get(), &object) != REDIS_OK)
                return error(ErrorCode::CorruptData, after, "invalid Redis reply");
            if (object) return Reply(static_cast<redisReply*>(object), &freeReplyObject);
        }
    }
};

bool text_is(const redisReply* r, const char* s) {
    return r && r->str && std::string_view(r->str, r->len) == s;
}

Error server_error(const redisReply* r, WriteEffect effect) {
    std::string_view s = r->str ? std::string_view(r->str, r->len) : std::string_view{};
    if (s.find("NOAUTH") == 0 || s.find("WRONGPASS") == 0 || s.find("NOPERM") == 0)
        return error(ErrorCode::PermissionDenied, effect, "Redis authorization denied");
    if (s.find("WRONGTYPE") == 0)
        return error(ErrorCode::SchemaMismatch, effect, "Redis key has incompatible type");
    if (s.find("MOVED") == 0 || s.find("ASK") == 0)
        return error(ErrorCode::Unsupported, effect, "Redis Cluster routing is not enabled");
    return error(ErrorCode::Unavailable, effect, "Redis server rejected request");
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

Result<bool> script_result(redisReply* r) {
    if (r->type == REDIS_REPLY_ERROR) return server_error(r, WriteEffect::Unknown);
    if (r->type != REDIS_REPLY_INTEGER)
        return error(ErrorCode::CorruptData, WriteEffect::Unknown,
                     "unexpected Redis script result");
    switch (r->integer) {
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
                         "Redis temporary record could not be published");
        default:
            return error(ErrorCode::CorruptData, WriteEffect::Unknown,
                         "unknown Redis script result");
    }
}
}  // namespace

struct RedisStore::Impl {
    RedisOptions options;
    BackendInfo capabilities;
    std::mutex mutex;
    std::condition_variable available;
    std::vector<std::unique_ptr<Connection>> connections;
    std::vector<bool> busy;
    std::atomic<bool> recovery_required{false};
    bool closed = false;

    struct Lease {
        Impl* p;
        std::size_t i;

        Lease(Impl* p, std::size_t i) : p(p), i(i) {}

        ~Lease() {
            std::lock_guard<std::mutex> lock(p->mutex);
            p->busy[i] = false;
            p->available.notify_one();
        }
    };

    Result<Reply> run(const std::vector<std::string>& args, const CallContext& ctx, bool write) {
        auto effect = write ? WriteEffect::NotApplied : WriteEffect::NotApplicable;
        std::unique_lock<std::mutex> lock(mutex);
        if (closed || recovery_required)
            return error(ErrorCode::NotReady, effect,
                         "Redis store closed or recovery authorization required");
        if (core::has_expired(ctx)) return network_error(ctx, effect);
        auto free = [&]() {
            return recovery_required || std::find(busy.begin(), busy.end(), false) != busy.end();
        };
        if (!available.wait_until(lock, ctx.deadline, free)) return network_error(ctx, effect);
        if (recovery_required)
            return error(ErrorCode::NotReady, effect,
                         "Redis connection recovery requires reopening and reauthorization");
        auto index =
            static_cast<std::size_t>(std::find(busy.begin(), busy.end(), false) - busy.begin());
        busy[index] = true;
        lock.unlock();
        Lease lease(this, index);
        auto r =
            connections[index]->command(args, ctx, write, options.max_record_bytes * 2 + 65536);
        if (!core::is_ok(r)) {
            recovery_required = true;
            available.notify_all();
        } else if (core::value_of(r)->type == REDIS_REPLY_ERROR) {
            // READONLY, role changes, script/transport failures require explicit recovery.
            auto err = server_error(core::value_of(r).get(),
                                    write ? WriteEffect::Unknown : WriteEffect::NotApplicable);
            if (err.code == ErrorCode::Unavailable || err.code == ErrorCode::Unsupported) {
                recovery_required = true;
                available.notify_all();
            }
        }
        return r;
    }
};

RedisStore::RedisStore(std::unique_ptr<Impl> p) : impl_(std::move(p)) {}

RedisStore::~RedisStore() = default;

BackendInfo RedisStore::info() const { return impl_->capabilities; }

void RedisStore::close() noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->closed = true;
    impl_->connections.clear();
    impl_->available.notify_all();
}

Result<std::unique_ptr<IRealtimeStore>> RedisStore::open(const RedisOptions& o,
                                                         const CallContext& ctx) {
    auto e = WriteEffect::NotApplied;
    if (core::has_expired(ctx)) return network_error(ctx, e);
    if (o.port == 0 || o.port > 65535 || o.database > 2147483647u || o.pool_size == 0 ||
        o.pool_size > 64 || o.max_record_bytes < 4 || o.max_record_bytes > 1048576 ||
        o.max_batch_items == 0 || o.max_batch_items > 65536 ||
        (!o.username.empty() && o.password.empty()))
        return error(ErrorCode::InvalidArgument, e, "invalid Redis options");
    auto p = std::make_unique<Impl>();
    p->options = o;
    for (std::size_t i = 0; i < o.pool_size; ++i) {
        auto c = std::make_unique<Connection>();
        auto connected = c->connect(o, ctx);
        if (!core::is_ok(connected)) return core::error_of(connected);
        std::vector<std::vector<std::string>> commands;
        if (!o.password.empty())
            commands.push_back(o.username.empty()
                                   ? std::vector<std::string>{"AUTH", o.password}
                                   : std::vector<std::string>{"AUTH", o.username, o.password});
        commands.push_back({"SELECT", std::to_string(o.database)});
        for (const auto& command : commands) {
            auto r = c->command(command, ctx, false, 65536);
            if (!core::is_ok(r)) return core::error_of(r);
            if (core::value_of(r)->type == REDIS_REPLY_ERROR)
                return server_error(core::value_of(r).get(), e);
            if (!text_is(core::value_of(r).get(), "OK"))
                return error(ErrorCode::SchemaMismatch, e,
                             "unexpected Redis initialization response");
        }
        auto role = c->command({"ROLE"}, ctx, false, 65536);
        if (!core::is_ok(role)) return core::error_of(role);
        auto* r = core::value_of(role).get();
        if (r->type == REDIS_REPLY_ERROR) return server_error(r, e);
        if (r->type != REDIS_REPLY_ARRAY || r->elements == 0 || !text_is(r->element[0], "master"))
            return error(ErrorCode::NotReady, e, "Redis endpoint is not a writable primary");
        auto info = c->command({"INFO", "server"}, ctx, false, 65536);
        if (!core::is_ok(info)) return core::error_of(info);
        auto* response = core::value_of(info).get();
        if (response->type == REDIS_REPLY_ERROR) return server_error(response, e);
        if (response->type != REDIS_REPLY_STRING)
            return error(ErrorCode::SchemaMismatch, e, "Redis server identity unavailable");
        std::string data(response->str, response->len);
        auto pos = data.find("run_id:");
        auto end = data.find("\r\n", pos);
        if (pos == std::string::npos || end == std::string::npos)
            return error(ErrorCode::SchemaMismatch, e, "Redis run ID unavailable");
        auto generation = data.substr(pos + 7, end - pos - 7);
        if (generation.empty() || generation.size() > 128)
            return error(ErrorCode::SchemaMismatch, e, "invalid Redis server identity");
        if (i == 0)
            p->capabilities = {"redis",          generation, true, true, false, o.max_record_bytes,
                               o.max_batch_items};
        else if (generation != p->capabilities.storage_generation)
            return error(ErrorCode::NotReady, e,
                         "Redis identity changed during pool initialization");
        p->connections.push_back(std::move(c));
        p->busy.push_back(false);
    }
    return std::unique_ptr<IRealtimeStore>(new RedisStore(std::move(p)));
}

Result<Record> RedisStore::read(const RecordKey& key, const CallContext& ctx) {
    auto e = WriteEffect::NotApplicable;
    if (!validate_record_key(key).empty())
        return error(ErrorCode::InvalidArgument, e, "invalid record key");
    auto r = impl_->run({"HGETALL", physical_key(key)}, ctx, false);
    if (!core::is_ok(r)) return core::error_of(r);
    auto* reply = core::value_of(r).get();
    if (reply->type == REDIS_REPLY_ERROR) return server_error(reply, e);
    if (reply->type != REDIS_REPLY_ARRAY || reply->elements % 2)
        return error(ErrorCode::CorruptData, e, "invalid Redis hash response");
    if (reply->elements == 0) return error(ErrorCode::NotFound, e, "record not found");
    Record out;
    for (std::size_t i = 0; i < reply->elements; i += 2) {
        auto* k = reply->element[i];
        auto* v = reply->element[i + 1];
        if (k->type != REDIS_REPLY_STRING || v->type != REDIS_REPLY_STRING)
            return error(ErrorCode::CorruptData, e, "invalid Redis hash field");
        std::string name(k->str, k->len), value(v->str, v->len);
        if (name == "_store_revision")
            out.revision.opaque = value;
        else if (!out.fields.emplace(name, value).second)
            return error(ErrorCode::CorruptData, e, "duplicate Redis field");
    }
    if (out.revision.empty() || out.revision.opaque.size() > 128)
        return error(ErrorCode::SchemaMismatch, e, "Redis record missing valid revision");
    if (!core::is_ok(encode_fields(out.fields, impl_->options.max_record_bytes, e)))
        return error(ErrorCode::CorruptData, e, "stored Redis fields violate contract");
    return out;
}

Result<WriteReceipt> RedisStore::compareExchange(const WriteRequest& req, const CallContext& ctx) {
    auto e = WriteEffect::NotApplied;
    if (!validate_record_key(req.key).empty() ||
        (req.expected && (req.expected->empty() || req.expected->opaque.size() > 128)))
        return error(ErrorCode::InvalidArgument, e, "invalid key/revision");
    auto encoded = encode_fields(req.replacement, impl_->options.max_record_bytes, e);
    if (!core::is_ok(encoded)) return core::error_of(encoded);
    auto nonce = token();
    if (!core::is_ok(nonce)) return core::error_of(nonce);
    auto revision = impl_->capabilities.storage_generation + ":" + core::value_of(nonce);
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    auto temp = "ems:" + hash_tag(req.key) + ":_store_tmp:" + std::to_string(now) + ":" +
                core::value_of(nonce);
    std::vector<std::string> args = {"EVAL",  cas_script,
                                     "2",     physical_key(req.key),
                                     temp,    req.expected ? req.expected->opaque : "",
                                     revision};
    for (const auto& f : req.replacement) {
        args.push_back(f.first);
        args.push_back(f.second);
    }
    auto r = impl_->run(args, ctx, true);
    if (!core::is_ok(r)) return core::error_of(r);
    auto status = script_result(core::value_of(r).get());
    if (!core::is_ok(status)) return core::error_of(status);
    return WriteReceipt{Revision{revision}};
}

Result<EraseReceipt> RedisStore::erase(const EraseRequest& req, const CallContext& ctx) {
    if (!validate_record_key(req.key).empty() || req.expected.empty() ||
        req.expected.opaque.size() > 128)
        return error(ErrorCode::InvalidArgument, WriteEffect::NotApplied, "invalid key/revision");
    auto r = impl_->run({"EVAL", erase_script, "1", physical_key(req.key), req.expected.opaque},
                        ctx, true);
    if (!core::is_ok(r)) return core::error_of(r);
    auto status = script_result(core::value_of(r).get());
    if (!core::is_ok(status)) return core::error_of(status);
    return EraseReceipt{};
}

Result<bool> register_redis_backend(BackendFactory& factory) {
    return factory.register_backend(
        "redis",
        [](const BackendSpec& spec,
           const CallContext& ctx) -> Result<std::unique_ptr<IRealtimeStore>> {
            if (!known_options(spec, {"address", "port", "database", "username", "password",
                                      "pool_size", "max_record_bytes", "max_batch_items"}))
                return error(ErrorCode::InvalidArgument, WriteEffect::NotApplied,
                             "unknown/unsupported Redis option (standalone numeric address only)");
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
                    else if (kv.first == "max_record_bytes")
                        o.max_record_bytes = n;
                    else
                        o.max_batch_items = n;
                }
            }
            return RedisStore::open(o, ctx);
        });
}
}  // namespace ems::realtime

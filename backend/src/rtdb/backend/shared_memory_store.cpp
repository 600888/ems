#include <rtdb/backend/shared_memory_store.h>

#include <algorithm>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <cstring>
#include <thread>

#include "store_detail.h"

namespace ems::rtdb {
namespace {
namespace bip = boost::interprocess;
using namespace detail;
using Segment = bip::managed_shared_memory;
using CharAllocator = bip::allocator<char, Segment::segment_manager>;
using SharedString = bip::basic_string<char, std::char_traits<char>, CharAllocator>;

struct KeyLess {
    using is_transparent = void;

    template <class A, class B>
    bool operator()(const A& a, const B& b) const {
        return std::string_view(a.data(), a.size()) < std::string_view(b.data(), b.size());
    }
};

using Pair = std::pair<const SharedString, SharedString>;
using PairAllocator = bip::allocator<Pair, Segment::segment_manager>;
using Records = bip::map<SharedString, SharedString, KeyLess, PairAllocator>;
using Guard = bip::scoped_lock<bip::named_mutex>;

struct Metadata {
    unsigned schema = 2;
    std::size_t capacity, shards, max_record, segment_bytes;
    char generation[33]{};

    Metadata(const SharedMemoryOptions& o, const std::string& id)
        : capacity(o.capacity),
          shards(o.shard_count),
          max_record(o.max_record_bytes),
          segment_bytes(o.segment_bytes) {
        std::memcpy(generation, id.c_str(), 32);
    }
};

bool valid_name(const std::string& name) {
    return !name.empty() && name.size() <= 64 &&
           std::all_of(name.begin(), name.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '-';
           });
}

std::string object_name(const std::string& name) { return "ems_rtdb_boost_" + name; }

Error boost_error(const bip::interprocess_exception& e, WriteEffect effect) {
    auto code = ErrorCode::Unavailable;
    if (e.get_error_code() == bip::not_found_error)
        code = ErrorCode::NotFound;
    else if (e.get_error_code() == bip::security_error)
        code = ErrorCode::PermissionDenied;
    else if (e.get_error_code() == bip::out_of_space_error ||
             e.get_error_code() == bip::out_of_memory_error)
        code = ErrorCode::ResourceExhausted;
    return error(code, effect, "Boost.Interprocess operation failed");
}
}  // namespace

struct SharedMemoryStore::Impl {
    SharedMemoryOptions options;
    BackendInfo capabilities;
    std::unique_ptr<bip::named_mutex> mutex;
    std::unique_ptr<Segment> segment;
    Records* records = nullptr;

    Result<std::unique_ptr<Guard>> lock(const CallContext& ctx, WriteEffect effect) {
        if (!segment) return error(ErrorCode::NotReady, effect, "shared-memory store closed");
        auto guard = std::make_unique<Guard>(*mutex, bip::defer_lock);
        while (!core::has_expired(ctx)) {
            if (guard->try_lock()) return guard;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return error(ErrorCode::DeadlineExceeded, effect,
                     "Boost lock deadline exceeded; abandoned locks require managed recovery");
    }

    Result<Record> decode(const SharedString& data, WriteEffect effect) const {
        std::string_view bytes(data.data(), data.size());
        Record out;
        if (!take_string(bytes, out.revision.opaque) || out.revision.empty() ||
            out.revision.opaque.size() > 128)
            return error(ErrorCode::CorruptData, effect, "shared record has invalid revision");
        auto fields = decode_fields(bytes, options.max_record_bytes, effect);
        if (!core::is_ok(fields)) return core::error_of(fields);
        out.fields = std::move(core::value_of(fields));
        return out;
    }
};

SharedMemoryStore::SharedMemoryStore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

SharedMemoryStore::~SharedMemoryStore() = default;

BackendInfo SharedMemoryStore::info() const { return impl_->capabilities; }

void SharedMemoryStore::close() noexcept {
    impl_->records = nullptr;
    impl_->segment.reset();
    impl_->mutex.reset();
}

Result<bool> SharedMemoryStore::remove(const std::string& name) {
    if (!valid_name(name))
        return error(ErrorCode::InvalidArgument, WriteEffect::NotApplied, "invalid segment name");
    const auto object = object_name(name);
    const bool segment_removed = bip::shared_memory_object::remove(object.c_str());
    const bool mutex_removed = bip::named_mutex::remove((object + "_mutex").c_str());
    return segment_removed || mutex_removed;
}

Result<std::unique_ptr<IRealtimeStore>> SharedMemoryStore::open(const SharedMemoryOptions& o,
                                                                const CallContext& ctx) {
    constexpr auto effect = WriteEffect::NotApplied;
    if (core::has_expired(ctx))
        return error(ErrorCode::DeadlineExceeded, effect, "shared-memory open deadline exceeded");
    if (!valid_name(o.segment_name) || !o.capacity || o.capacity > 1048576 || !o.shard_count ||
        o.shard_count > 256 || o.capacity % o.shard_count || o.max_record_bytes < 4 ||
        o.max_record_bytes > 1048576 || !o.max_batch_items || o.max_batch_items > 65536 ||
        o.segment_bytes < 4096 || o.segment_bytes > 2147483648ULL)
        return error(ErrorCode::InvalidArgument, effect, "invalid shared-memory options");
    try {
        auto p = std::make_unique<Impl>();
        p->options = o;
        const auto name = object_name(o.segment_name);
        if (o.create_if_missing)
            p->mutex =
                std::make_unique<bip::named_mutex>(bip::open_or_create, (name + "_mutex").c_str());
        else
            p->mutex =
                std::make_unique<bip::named_mutex>(bip::open_only, (name + "_mutex").c_str());
        Guard bootstrap(*p->mutex, bip::defer_lock);
        while (!bootstrap.try_lock()) {
            if (core::has_expired(ctx))
                return error(ErrorCode::NotReady, effect, "shared-memory initialization locked");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        bool created = false;
        if (o.create_if_missing) {
            try {
                p->segment =
                    std::make_unique<Segment>(bip::create_only, name.c_str(), o.segment_bytes);
                created = true;
            } catch (const bip::interprocess_exception& e) {
                if (e.get_error_code() != bip::already_exists_error) throw;
            }
        }
        if (!p->segment) p->segment = std::make_unique<Segment>(bip::open_only, name.c_str());
        if (created) {
            auto generation = token();
            if (!core::is_ok(generation)) return core::error_of(generation);
            p->segment->construct<Metadata>("metadata")(o, core::value_of(generation));
            p->segment->construct<Records>("records")(
                KeyLess{}, PairAllocator(p->segment->get_segment_manager()));
        }
        auto metadata = p->segment->find<Metadata>("metadata");
        auto records = p->segment->find<Records>("records");
        if (!metadata.first || metadata.second != 1 || !records.first || records.second != 1)
            return error(ErrorCode::NotReady, effect, "Boost segment initialization incomplete");
        const auto& m = *metadata.first;
        if (m.schema != 2 || m.capacity != o.capacity || m.shards != o.shard_count ||
            m.max_record != o.max_record_bytes || m.segment_bytes != o.segment_bytes ||
            p->segment->get_size() != o.segment_bytes || m.generation[32] != '\0')
            return error(ErrorCode::SchemaMismatch, effect,
                         "Boost segment layout/options mismatch");
        const std::string generation(m.generation, 32);
        if (!o.expected_generation.empty() && o.expected_generation != generation)
            return error(ErrorCode::NotReady, effect, "shared-memory generation mismatch");
        p->records = records.first;
        p->capabilities = {
            "shm", generation, true, false, false, o.max_record_bytes, o.max_batch_items};
        if (core::has_expired(ctx))
            return error(ErrorCode::DeadlineExceeded, effect,
                         "shared-memory open budget exhausted");
        return std::unique_ptr<IRealtimeStore>(new SharedMemoryStore(std::move(p)));
    } catch (const bip::interprocess_exception& e) {
        return boost_error(e, effect);
    } catch (const std::bad_alloc&) {
        return error(ErrorCode::ResourceExhausted, effect, "shared-memory allocation failed");
    }
}

Result<Record> SharedMemoryStore::read(const RecordKey& key, const CallContext& ctx) {
    constexpr auto effect = WriteEffect::NotApplicable;
    if (!validate_record_key(key).empty())
        return error(ErrorCode::InvalidArgument, effect, "invalid record key");
    try {
        auto guard = impl_->lock(ctx, effect);
        if (!core::is_ok(guard)) return core::error_of(guard);
        auto it = impl_->records->find(physical_key(key));
        if (it == impl_->records->end())
            return error(ErrorCode::NotFound, effect, "record not found");
        return impl_->decode(it->second, effect);
    } catch (const bip::interprocess_exception& e) {
        return boost_error(e, effect);
    }
}

Result<std::vector<RecordKey>> SharedMemoryStore::listAssets(const Scope& scope,
                                                             const CallContext& ctx) {
    constexpr auto effect = WriteEffect::NotApplicable;
    if (!validate_record_key({scope, RecordKind::AssetState, {}, {}}).empty())
        return error(ErrorCode::InvalidArgument, effect, "invalid scope");
    try {
        auto guard = impl_->lock(ctx, effect);
        if (!core::is_ok(guard)) return core::error_of(guard);
        std::vector<RecordKey> out;
        for (const auto& item : *impl_->records) {
            if (core::has_expired(ctx))
                return error(ErrorCode::DeadlineExceeded, effect, "enumeration deadline exceeded");
            auto key = asset_key(scope, {item.first.data(), item.first.size()});
            if (!key) continue;
            if (out.size() == 65536)
                return error(ErrorCode::ResourceExhausted, effect, "enumeration limit exceeded");
            out.push_back(std::move(*key));
        }
        return out;
    } catch (const bip::interprocess_exception& e) {
        return boost_error(e, effect);
    }
}

Result<WriteReceipt> SharedMemoryStore::compareExchange(const WriteRequest& req,
                                                        const CallContext& ctx) {
    constexpr auto effect = WriteEffect::NotApplied;
    if (!validate_record_key(req.key).empty() ||
        (req.expected && (req.expected->empty() || req.expected->opaque.size() > 128)))
        return error(ErrorCode::InvalidArgument, effect, "invalid key/revision");
    auto encoded = encode_fields(req.replacement, impl_->options.max_record_bytes, effect);
    if (!core::is_ok(encoded)) return core::error_of(encoded);
    auto nonce = token();
    if (!core::is_ok(nonce)) return core::error_of(nonce);
    try {
        auto guard = impl_->lock(ctx, effect);
        if (!core::is_ok(guard)) return core::error_of(guard);
        const auto key = physical_key(req.key);
        auto it = impl_->records->find(key);
        if (req.expected) {
            if (it == impl_->records->end())
                return error(ErrorCode::NotFound, effect, "record not found");
            auto current = impl_->decode(it->second, effect);
            if (!core::is_ok(current)) return core::error_of(current);
            if (!(core::value_of(current).revision == *req.expected))
                return error(ErrorCode::Conflict, effect, "revision conflict");
        } else if (it != impl_->records->end()) {
            return error(ErrorCode::Conflict, effect, "record already exists");
        }
        if (it == impl_->records->end() && impl_->records->size() >= impl_->options.capacity)
            return error(ErrorCode::ResourceExhausted, effect,
                         "shared-memory record capacity reached");
        auto revision = impl_->capabilities.storage_generation + ":" + core::value_of(nonce);
        std::string bytes;
        append_string(bytes, revision);
        bytes += core::value_of(encoded);
        CharAllocator allocator(impl_->segment->get_segment_manager());
        SharedString replacement(bytes.data(), bytes.size(), allocator);
        if (it == impl_->records->end()) {
            SharedString stored_key(key.data(), key.size(), allocator);
            impl_->records->emplace(std::move(stored_key), std::move(replacement));
        } else {
            it->second.swap(replacement);
        }
        return WriteReceipt{Revision{std::move(revision)}};
    } catch (const bip::interprocess_exception& e) {
        return boost_error(e, effect);
    } catch (const std::bad_alloc&) {
        return error(ErrorCode::ResourceExhausted, effect, "shared-memory allocation failed");
    }
}

Result<EraseReceipt> SharedMemoryStore::erase(const EraseRequest& req, const CallContext& ctx) {
    constexpr auto effect = WriteEffect::NotApplied;
    if (!validate_record_key(req.key).empty() || req.expected.empty() ||
        req.expected.opaque.size() > 128)
        return error(ErrorCode::InvalidArgument, effect, "invalid key/revision");
    try {
        auto guard = impl_->lock(ctx, effect);
        if (!core::is_ok(guard)) return core::error_of(guard);
        auto it = impl_->records->find(physical_key(req.key));
        if (it == impl_->records->end())
            return error(ErrorCode::NotFound, effect, "record not found");
        auto current = impl_->decode(it->second, effect);
        if (!core::is_ok(current)) return core::error_of(current);
        if (!(core::value_of(current).revision == req.expected))
            return error(ErrorCode::Conflict, effect, "revision conflict");
        impl_->records->erase(it);
        return EraseReceipt{};
    } catch (const bip::interprocess_exception& e) {
        return boost_error(e, effect);
    }
}

Result<bool> register_shared_memory_backend(BackendFactory& factory) {
    return factory.register_backend(
        "shm",
        [](const BackendSpec& spec,
           const CallContext& ctx) -> Result<std::unique_ptr<IRealtimeStore>> {
            if (!known_options(
                    spec, {"segment_name", "create_if_missing", "expected_generation", "capacity",
                           "shard_count", "max_record_bytes", "max_batch_items", "segment_bytes"}))
                return error(ErrorCode::InvalidArgument, WriteEffect::NotApplied,
                             "unknown shared-memory option");
            SharedMemoryOptions o;
            for (const auto& kv : spec.options) {
                if (kv.first == "segment_name")
                    o.segment_name = kv.second;
                else if (kv.first == "expected_generation")
                    o.expected_generation = kv.second;
                else if (kv.first == "create_if_missing") {
                    if (kv.second != "true" && kv.second != "false")
                        return error(ErrorCode::InvalidArgument, WriteEffect::NotApplied,
                                     "invalid boolean");
                    o.create_if_missing = kv.second == "true";
                } else {
                    std::size_t n;
                    if (!parse_size(kv.second, n))
                        return error(ErrorCode::InvalidArgument, WriteEffect::NotApplied,
                                     "invalid size option");
                    if (kv.first == "capacity")
                        o.capacity = n;
                    else if (kv.first == "shard_count")
                        o.shard_count = n;
                    else if (kv.first == "max_record_bytes")
                        o.max_record_bytes = n;
                    else if (kv.first == "max_batch_items")
                        o.max_batch_items = n;
                    else
                        o.segment_bytes = n;
                }
            }
            return SharedMemoryStore::open(o, ctx);
        });
}
}  // namespace ems::rtdb

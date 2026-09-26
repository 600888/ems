#include <realtime/shared_memory_store.h>

#include <algorithm>
#include <cstring>
#include <thread>
#include <utility>

#include "store_detail.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace ems::realtime {
namespace {
using namespace detail;
constexpr std::uint32_t layout_version = 1, ready = 2;
constexpr std::uint64_t magic = 0x315452534d454d53ULL;

constexpr std::size_t align64(std::size_t n) { return (n + 63) & ~std::size_t(63); }

struct alignas(64) Header {
    std::uint64_t magic_value;
    std::uint32_t version, endian, header_size, sync_size;
    std::uint64_t size, capacity, shards, max_record, slot_stride, image_stride;
    std::uint64_t shards_offset, slots_offset;
    char generation[32];
    alignas(4) std::uint32_t state;
};

struct alignas(64) Shard {
#ifndef _WIN32
    pthread_mutex_t mutex;
#endif
    std::uint32_t broken;
};

struct alignas(64) Slot {
    alignas(4) std::uint32_t active;
};

struct Image {
    std::uint32_t length, crc;
};

std::uint32_t load_word(std::uint32_t* word) {
#ifdef _WIN32
    return static_cast<std::uint32_t>(
        InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(word), 0, 0));
#else
    static_assert(__atomic_always_lock_free(4, nullptr),
                  "shared-memory commit requires lock-free aligned 32-bit atomic");
    return __atomic_load_n(word, __ATOMIC_ACQUIRE);
#endif
}

void publish_word(std::uint32_t* word, std::uint32_t value) {
#ifdef _WIN32
    InterlockedExchange(reinterpret_cast<volatile LONG*>(word), static_cast<LONG>(value));
#else
    __atomic_store_n(word, value, __ATOMIC_RELEASE);
#endif
}

std::uint32_t sync_abi() {
#ifdef _WIN32
    return 0x57494e00u + sizeof(void*);
#else
    return 0x4c4e0000u + sizeof(pthread_mutex_t) * 256u + alignof(pthread_mutex_t) * 16u +
           sizeof(void*);
#endif
}

bool valid_name(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            return false;
    return true;
}

Error timeout(WriteEffect e) {
    return error(ErrorCode::DeadlineExceeded, e, "shared-memory deadline exceeded");
}

Error corrupt(WriteEffect e) {
    return error(ErrorCode::CorruptData, e,
                 "shared-memory shard quarantined: invalid active image");
}
#ifdef EMS_REALTIME_TEST_HOOKS
void (*commit_hook)(int) = nullptr;
#endif
}  // namespace
#ifdef EMS_REALTIME_TEST_HOOKS
void set_shared_memory_test_hook(void (*hook)(int)) { commit_hook = hook; }
#endif

struct SharedMemoryStore::Impl {
    SharedMemoryOptions options;
    BackendInfo capabilities;
    void* memory = nullptr;
    Header* header = nullptr;
    std::size_t mapped_size = 0, image_stride = 0, slot_stride = 0, slots_offset = 0;
#ifdef _WIN32
    HANDLE mapping = nullptr;
    std::vector<HANDLE> locks;
#else
    int fd = -1;
#endif
    ~Impl() { close(); }

    void close() noexcept {
#ifdef _WIN32
        for (auto lock : locks) CloseHandle(lock);
        locks.clear();
        if (memory) UnmapViewOfFile(memory);
        if (mapping) CloseHandle(mapping);
        mapping = nullptr;
#else
        if (memory) munmap(memory, mapped_size);
        if (fd >= 0) ::close(fd);
        fd = -1;
#endif
        memory = nullptr;
        header = nullptr;
    }

    Shard* shard(std::size_t i) const {
        return reinterpret_cast<Shard*>(static_cast<char*>(memory) + sizeof(Header)) + i;
    }

    Slot* slot(std::size_t i) const {
        return reinterpret_cast<Slot*>(static_cast<char*>(memory) + slots_offset + i * slot_stride);
    }

    Image* image(Slot* s, std::uint32_t index) const {
        return reinterpret_cast<Image*>(reinterpret_cast<char*>(s) + sizeof(Slot) +
                                        index * image_stride);
    }

    struct Stored {
        bool live = false;
        std::string key, revision;
        Fields fields;
    };

    Result<Stored> inspect(Slot* s, WriteEffect effect) {
        const auto active = load_word(&s->active);
        if (active == 0) return Stored{};
        if (active > 2) return corrupt(effect);
        const auto* img = image(s, active - 1);
        if (img->length > image_stride - sizeof(Image)) return corrupt(effect);
        std::string_view bytes(reinterpret_cast<const char*>(img) + sizeof(Image), img->length);
        if (checksum(bytes) != img->crc) return corrupt(effect);
        std::uint32_t state;
        Stored out;
        if (!take_u32(bytes, state) || (state != 1 && state != 2) || !take_string(bytes, out.key) ||
            out.key.empty() || out.key.size() > max_key_bytes ||
            !take_string(bytes, out.revision) || out.revision.empty() || out.revision.size() > 128)
            return corrupt(effect);
        auto fields = decode_fields(bytes, options.max_record_bytes, effect);
        if (!core::is_ok(fields)) return corrupt(effect);
        out.fields = std::move(core::value_of(fields));
        out.live = state == 1;
        return out;
    }
    enum class LockAttempt { Busy, Acquired, Abandoned };

    Result<LockAttempt> try_lock_shard(std::size_t id, [[maybe_unused]] const CallContext& ctx,
                                       WriteEffect effect) {
#ifdef _WIN32
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(core::remaining_budget(ctx))
                .count();
        const auto wait_ms =
            static_cast<DWORD>(std::min<std::int64_t>(std::max<std::int64_t>(1, remaining), 50));
        const auto status = WaitForSingleObject(locks[id], wait_ms);
        if (status == WAIT_OBJECT_0) return LockAttempt::Acquired;
        if (status == WAIT_ABANDONED) return LockAttempt::Abandoned;
        if (status == WAIT_TIMEOUT) return LockAttempt::Busy;
        return error(ErrorCode::Unavailable, effect, "shared-memory mutex wait failed");
#else
        const auto status = pthread_mutex_trylock(&shard(id)->mutex);
        if (status == 0) return LockAttempt::Acquired;
        if (status == EOWNERDEAD) return LockAttempt::Abandoned;
        if (status != EBUSY)
            return error(ErrorCode::CorruptData, effect, "shared-memory mutex unrecoverable");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return LockAttempt::Busy;
#endif
    }

    void unlock_shard(std::size_t id) noexcept {
#ifdef _WIN32
        ReleaseMutex(locks[id]);
#else
        pthread_mutex_unlock(&shard(id)->mutex);
#endif
    }

    bool make_shard_consistent([[maybe_unused]] std::size_t id) noexcept {
#ifdef _WIN32
        return true;
#else
        return pthread_mutex_consistent(&shard(id)->mutex) == 0;
#endif
    }

    struct Guard {
        Impl* owner;
        std::size_t id;

        Guard(Impl* o, std::size_t i) : owner(o), id(i) {}

        Guard(const Guard&) = delete;

        Guard(Guard&& other) noexcept : owner(std::exchange(other.owner, nullptr)), id(other.id) {}

        ~Guard() {
            if (owner) owner->unlock_shard(id);
        }
    };

    Result<Guard> lock(std::size_t id, const CallContext& ctx, WriteEffect effect) {
        auto attempt = LockAttempt::Busy;
        while (true) {
            if (core::has_expired(ctx)) return timeout(effect);
            auto result = try_lock_shard(id, ctx, effect);
            if (!core::is_ok(result)) return core::error_of(result);
            attempt = core::value_of(result);
            if (attempt != LockAttempt::Busy) break;
        }
        Guard guard(this, id);
        if (attempt == LockAttempt::Abandoned) {
            auto count = options.capacity / options.shard_count;
            for (std::size_t i = id * count; i < (id + 1) * count; ++i)
                if (!core::is_ok(inspect(slot(i), effect))) {
                    shard(id)->broken = 1;
                    break;
                }
            if (!make_shard_consistent(id)) shard(id)->broken = 1;
        }
        return std::move(guard);
    }

    Result<std::pair<Slot*, Stored>> find(const std::string& key, std::size_t id,
                                          WriteEffect effect) {
        if (shard(id)->broken) return corrupt(effect);
        Slot* vacant = nullptr;
        auto count = options.capacity / options.shard_count;
        for (std::size_t i = id * count; i < (id + 1) * count; ++i) {
            auto* entry = slot(i);
            auto value = inspect(entry, effect);
            if (!core::is_ok(value)) {
                shard(id)->broken = 1;
                return corrupt(effect);
            }
            auto& stored = core::value_of(value);
            if (stored.live && stored.key == key) return std::make_pair(entry, std::move(stored));
            if (!stored.live && !vacant) vacant = entry;
        }
        return std::make_pair(vacant, Stored{});
    }

    Result<Revision> commit(Slot* s, const std::string& key, const std::string& encoded,
                            bool tombstone, const CallContext& ctx) {
        auto nonce = token();
        if (!core::is_ok(nonce)) return core::error_of(nonce);
        Revision rev{capabilities.storage_generation + ":" + core::value_of(nonce)};
        std::string payload;
        append_u32(payload, tombstone ? 2 : 1);
        append_string(payload, key);
        append_string(payload, rev.opaque);
        payload += encoded;
        auto active = load_word(&s->active);
        auto next = active == 1 ? 1u : 0u;
        auto* img = image(s, next);
        img->length = static_cast<std::uint32_t>(payload.size());
        std::memcpy(reinterpret_cast<char*>(img) + sizeof(Image), payload.data(), payload.size());
        img->crc = checksum(payload);
#ifdef EMS_REALTIME_TEST_HOOKS
        if (commit_hook) commit_hook(1);
#endif
        if (core::has_expired(ctx)) return timeout(WriteEffect::NotApplied);
        publish_word(
            &s->active,
            next + 1);  // the sole commit point; inactive writes never become visible on recovery
#ifdef EMS_REALTIME_TEST_HOOKS
        if (commit_hook) commit_hook(2);
#endif
        return rev;
    }
};

SharedMemoryStore::SharedMemoryStore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

SharedMemoryStore::~SharedMemoryStore() = default;

BackendInfo SharedMemoryStore::info() const { return impl_->capabilities; }

void SharedMemoryStore::close() noexcept { impl_->close(); }

Result<std::unique_ptr<IRealtimeStore>> SharedMemoryStore::open(const SharedMemoryOptions& o,
                                                                const CallContext& ctx) {
    constexpr auto no = WriteEffect::NotApplied;
    if (core::has_expired(ctx)) return timeout(no);
    const std::uint32_t endian = 1;
    if (*reinterpret_cast<const unsigned char*>(&endian) != 1)
        return error(ErrorCode::Unsupported, no, "shared-memory v1 requires little endian");
    if (!valid_name(o.segment_name) || o.capacity == 0 || o.capacity > 1048576 ||
        o.shard_count == 0 || o.shard_count > 256 || o.capacity % o.shard_count != 0 ||
        o.max_record_bytes < 4 || o.max_record_bytes > 1048576 || o.max_batch_items == 0 ||
        o.max_batch_items > 65536)
        return error(ErrorCode::InvalidArgument, no, "invalid shared-memory dimensions/name");
    auto p = std::make_unique<Impl>();
    p->options = o;
    p->image_stride = align64(sizeof(Image) + 4 + 4 + max_key_bytes + 4 + 128 + o.max_record_bytes);
    p->slot_stride = sizeof(Slot) + 2 * p->image_stride;
    p->slots_offset = sizeof(Header) + o.shard_count * sizeof(Shard);
    // Explicitly bound the allocation (and arithmetic) before any mapping.
    if (o.segment_bytes > 2147483648ULL || o.segment_bytes < p->slots_offset ||
        o.capacity > (o.segment_bytes - p->slots_offset) / p->slot_stride)
        return error(ErrorCode::InvalidArgument, no,
                     "segment budget cannot contain requested slots");
    p->mapped_size = p->slots_offset + o.capacity * p->slot_stride;
    bool creator = false;
#ifdef _WIN32
    const std::string name = "Local\\ems_rt_" + o.segment_name;
    HANDLE bootstrap = CreateMutexA(nullptr, FALSE, (name + "_init").c_str());
    if (!bootstrap)
        return error(ErrorCode::PermissionDenied, no,
                     "cannot open shared-memory initialization lock");

    struct Boot {
        HANDLE h;
        bool held = false;

        ~Boot() {
            if (held) ReleaseMutex(h);
            CloseHandle(h);
        }
    } boot{bootstrap};

    while (!boot.held) {
        if (core::has_expired(ctx)) return timeout(no);
        auto r = WaitForSingleObject(bootstrap, 1);
        if (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED)
            boot.held = true;
        else if (r != WAIT_TIMEOUT)
            return error(ErrorCode::Unavailable, no, "initialization lock failed");
    }
    if (o.create_if_missing) {
        p->mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                        static_cast<DWORD>(p->mapped_size), name.c_str());
        auto status = GetLastError();
        creator = p->mapping && status != ERROR_ALREADY_EXISTS;
    } else
        p->mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (!p->mapping)
        return error(GetLastError() == ERROR_FILE_NOT_FOUND ? ErrorCode::NotFound
                                                            : ErrorCode::PermissionDenied,
                     no, "cannot open shared-memory mapping");
    // Map only the requested verified extent; an undersized existing object fails safely.
    p->memory = MapViewOfFile(p->mapping, FILE_MAP_ALL_ACCESS, 0, 0, p->mapped_size);
    if (!p->memory)
        return error(ErrorCode::SchemaMismatch, no, "mapping size mismatch or mapping failed");
#else
    const std::string name = "/ems_rt_" + o.segment_name;
    if (o.create_if_missing) {
        p->fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
        if (p->fd >= 0)
            creator = true;
        else if (errno == EEXIST)
            p->fd = shm_open(name.c_str(), O_RDWR, 0600);
    } else
        p->fd = shm_open(name.c_str(), O_RDWR, 0600);
    if (p->fd < 0)
        return error(errno == ENOENT ? ErrorCode::NotFound : ErrorCode::PermissionDenied, no,
                     "cannot open shared-memory object");
    if (creator && ftruncate(p->fd, static_cast<off_t>(p->mapped_size)) != 0)
        return error(ErrorCode::ResourceExhausted, no, "cannot size shared-memory object");
    struct stat st{};
    do {
        if (fstat(p->fd, &st) != 0)
            return error(ErrorCode::Unavailable, no, "cannot inspect shared-memory object");
        if (st.st_size != 0) break;
        if (core::has_expired(ctx)) return timeout(no);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (true);
    if (st.st_size != static_cast<off_t>(p->mapped_size))
        return error(ErrorCode::SchemaMismatch, no, "shared-memory size mismatch");
    auto mapped = mmap(nullptr, p->mapped_size, PROT_READ | PROT_WRITE, MAP_SHARED, p->fd, 0);
    if (mapped == MAP_FAILED)
        return error(ErrorCode::ResourceExhausted, no, "cannot map shared-memory object");
    p->memory = mapped;
#endif
    p->header = static_cast<Header*>(p->memory);
    if (creator) {
        // Newly created OS mappings are zero-filled. Publish READY only after all metadata and locks.
        auto gen = token();
        if (!core::is_ok(gen)) return core::error_of(gen);
        auto& h = *p->header;
        h.magic_value = magic;
        h.version = layout_version;
        h.endian = 0x01020304;
        h.header_size = sizeof(Header);
        h.sync_size = sync_abi();
        h.size = p->mapped_size;
        h.capacity = o.capacity;
        h.shards = o.shard_count;
        h.max_record = o.max_record_bytes;
        h.slot_stride = p->slot_stride;
        h.image_stride = p->image_stride;
        h.shards_offset = sizeof(Header);
        h.slots_offset = p->slots_offset;
        std::memcpy(h.generation, core::value_of(gen).data(), 32);
#ifndef _WIN32
        pthread_mutexattr_t attr;
        if (pthread_mutexattr_init(&attr) != 0)
            return error(ErrorCode::Unsupported, no, "mutex attributes unavailable");
        auto status = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        if (status == 0) status = pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
        for (std::size_t i = 0; status == 0 && i < o.shard_count; ++i)
            status = pthread_mutex_init(&p->shard(i)->mutex, &attr);
        pthread_mutexattr_destroy(&attr);
        if (status != 0)
            return error(ErrorCode::Unsupported, no, "robust process-shared mutex unavailable");
#endif
        publish_word(&h.state, ready);
    }
    while (load_word(&p->header->state) != ready) {
        if (core::has_expired(ctx))
            return error(ErrorCode::NotReady, no,
                         "segment initialization incomplete; administrative recovery required");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto& h = *p->header;
    if (h.magic_value != magic || h.version != layout_version || h.endian != 0x01020304 ||
        h.header_size != sizeof(Header) || h.sync_size != sync_abi() || h.size != p->mapped_size ||
        h.capacity != o.capacity || h.shards != o.shard_count ||
        h.max_record != o.max_record_bytes || h.slot_stride != p->slot_stride ||
        h.image_stride != p->image_stride || h.shards_offset != sizeof(Header) ||
        h.slots_offset != p->slots_offset)
        return error(ErrorCode::SchemaMismatch, no, "shared-memory layout/ABI/options mismatch");
    p->capabilities = {"shm",
                       std::string(h.generation, 32),
                       true,
                       false,
                       false,
                       o.max_record_bytes,
                       o.max_batch_items};
    if (!o.expected_generation.empty() &&
        o.expected_generation != p->capabilities.storage_generation)
        return error(ErrorCode::NotReady, no, "shared-memory generation fence mismatch");
#ifdef _WIN32
    for (std::size_t i = 0; i < o.shard_count; ++i) {
        auto lock = CreateMutexA(
            nullptr, FALSE,
            (name + "_" + p->capabilities.storage_generation + "_" + std::to_string(i)).c_str());
        if (!lock) return error(ErrorCode::PermissionDenied, no, "cannot open shard mutex");
        p->locks.push_back(lock);
    }
#endif
    if (core::has_expired(ctx)) return timeout(no);
    return std::unique_ptr<IRealtimeStore>(new SharedMemoryStore(std::move(p)));
}

Result<Record> SharedMemoryStore::read(const RecordKey& key, const CallContext& ctx) {
    constexpr auto e = WriteEffect::NotApplicable;
    if (!impl_->memory) return error(ErrorCode::NotReady, e, "shared-memory store closed");
    if (core::has_expired(ctx)) return timeout(e);
    if (!validate_record_key(key).empty())
        return error(ErrorCode::InvalidArgument, e, "invalid record key");
    auto encoded = physical_key(key);
    auto id = hash(encoded) % impl_->options.shard_count;
    auto locked = impl_->lock(id, ctx, e);
    if (!core::is_ok(locked)) return core::error_of(locked);
    auto guard = std::get<Impl::Guard>(std::move(locked));
    if (core::has_expired(ctx)) return timeout(e);
    auto result = impl_->find(encoded, id, e);
    if (!core::is_ok(result)) return core::error_of(result);
    auto& stored = core::value_of(result).second;
    if (!stored.live) return error(ErrorCode::NotFound, e, "record not found");
    return Record{std::move(stored.fields), Revision{stored.revision}};
}

Result<WriteReceipt> SharedMemoryStore::compareExchange(const WriteRequest& req,
                                                        const CallContext& ctx) {
    constexpr auto e = WriteEffect::NotApplied;
    if (!impl_->memory) return error(ErrorCode::NotReady, e, "shared-memory store closed");
    if (core::has_expired(ctx)) return timeout(e);
    if (!validate_record_key(req.key).empty() ||
        (req.expected && (req.expected->empty() || req.expected->opaque.size() > 128)))
        return error(ErrorCode::InvalidArgument, e, "invalid key or revision");
    auto fields = encode_fields(req.replacement, impl_->options.max_record_bytes, e);
    if (!core::is_ok(fields)) return core::error_of(fields);
    auto key = physical_key(req.key);
    auto id = hash(key) % impl_->options.shard_count;
    auto locked = impl_->lock(id, ctx, e);
    if (!core::is_ok(locked)) return core::error_of(locked);
    auto guard = std::get<Impl::Guard>(std::move(locked));
    auto found = impl_->find(key, id, e);
    if (!core::is_ok(found)) return core::error_of(found);
    auto& pair = core::value_of(found);
    if (!pair.second.live && req.expected) return error(ErrorCode::NotFound, e, "record not found");
    if (pair.second.live && (!req.expected || req.expected->opaque != pair.second.revision))
        return error(ErrorCode::Conflict, e, "revision conflict");
    if (!pair.first) return error(ErrorCode::ResourceExhausted, e, "shared-memory shard full");
    auto rev = impl_->commit(pair.first, key, core::value_of(fields), false, ctx);
    if (!core::is_ok(rev)) return core::error_of(rev);
    return WriteReceipt{core::value_of(rev)};
}

Result<EraseReceipt> SharedMemoryStore::erase(const EraseRequest& req, const CallContext& ctx) {
    constexpr auto e = WriteEffect::NotApplied;
    if (!impl_->memory) return error(ErrorCode::NotReady, e, "shared-memory store closed");
    if (core::has_expired(ctx)) return timeout(e);
    if (!validate_record_key(req.key).empty() || req.expected.empty() ||
        req.expected.opaque.size() > 128)
        return error(ErrorCode::InvalidArgument, e, "invalid key or revision");
    auto key = physical_key(req.key);
    auto id = hash(key) % impl_->options.shard_count;
    auto locked = impl_->lock(id, ctx, e);
    if (!core::is_ok(locked)) return core::error_of(locked);
    auto guard = std::get<Impl::Guard>(std::move(locked));
    auto found = impl_->find(key, id, e);
    if (!core::is_ok(found)) return core::error_of(found);
    auto& pair = core::value_of(found);
    if (!pair.second.live) return error(ErrorCode::NotFound, e, "record not found");
    if (pair.second.revision != req.expected.opaque)
        return error(ErrorCode::Conflict, e, "revision conflict");
    auto rev = impl_->commit(pair.first, key, std::string(4, '\0'), true, ctx);
    if (!core::is_ok(rev)) return core::error_of(rev);
    return EraseReceipt{};
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
}  // namespace ems::realtime

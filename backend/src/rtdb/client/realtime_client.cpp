#include <rtdb/client/realtime_client.h>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "store_detail.h"
#ifdef EMS_REALTIME_WITH_SHM
#include <rtdb/backend/shared_memory_store.h>
#endif
#ifdef EMS_REALTIME_WITH_REDIS
#include <rtdb/backend/redis_store.h>
#endif

namespace ems::rtdb {
namespace {
Result<std::unique_ptr<IRealtimeStore>> open_configuration(const std::string& path,
                                                           bool initializer,
                                                           const CallContext& ctx) {
    using namespace detail;
    constexpr auto effect = WriteEffect::NotApplied;
    if (core::has_expired(ctx))
        return error(ErrorCode::DeadlineExceeded, effect, "client initialization budget exhausted");
    try {
        std::ifstream input(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
        if (!input) return error(ErrorCode::NotFound, effect, "realtime configuration unavailable");
        const auto size = input.tellg();
        if (size <= 0 || size > 1048576)
            return error(ErrorCode::InvalidArgument, effect, "configuration size is invalid");
        input.seekg(0);
        std::string bytes(static_cast<std::size_t>(size), '\0');
        if (!input.read(bytes.data(), size))
            return error(ErrorCode::Unavailable, effect, "configuration read failed");
        const auto root = nlohmann::json::parse(bytes);
        const auto& backend = root.at("realtime").at("backend");
        if (!backend.is_object() || !backend.at("type").is_string())
            return error(ErrorCode::InvalidArgument, effect, "invalid realtime backend definition");
        BackendSpec spec;
        spec.type = backend.at("type").get<std::string>();
        for (const auto& field : backend.items())
            if (field.key() != "type" && field.key() != "options")
                return error(ErrorCode::Unsupported, effect,
                             "unsupported backend configuration field");
        if (backend.contains("options")) {
            const auto& options = backend.at("options");
            if (!options.is_object())
                return error(ErrorCode::InvalidArgument, effect,
                             "backend options must be an object");
            for (const auto& item : options.items()) {
                const auto& value = item.value();
                if (value.is_string())
                    spec.options[item.key()] = value.get<std::string>();
                else if (value.is_boolean() || value.is_number_integer())
                    spec.options[item.key()] = value.dump();
                else
                    return error(ErrorCode::InvalidArgument, effect, "invalid backend option type");
            }
        }
        if (spec.options.count("create_if_missing"))
            return error(ErrorCode::InvalidArgument, effect,
                         "creation privilege is not configurable");
        if (spec.type == "shm") spec.options["create_if_missing"] = initializer ? "true" : "false";
        BackendFactory factory;
#ifdef EMS_REALTIME_WITH_SHM
        auto shm = register_shared_memory_backend(factory);
        if (!core::is_ok(shm)) return core::error_of(shm);
#endif
#ifdef EMS_REALTIME_WITH_REDIS
        auto redis = register_redis_backend(factory);
        if (!core::is_ok(redis)) return core::error_of(redis);
#endif
        factory.freeze();
        return factory.create(spec, ctx);
    } catch (const nlohmann::json::exception&) {
        return error(ErrorCode::InvalidArgument, effect, "invalid realtime JSON configuration");
    } catch (const std::filesystem::filesystem_error&) {
        return error(ErrorCode::InvalidArgument, effect, "invalid configuration path");
    } catch (const std::bad_alloc&) {
        return error(ErrorCode::ResourceExhausted, effect, "configuration allocation failed");
    }
}
}  // namespace

struct RealtimeClient::Impl {
    std::unique_ptr<IRealtimeStore> store;
};

RealtimeClient::RealtimeClient(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

RealtimeClient::~RealtimeClient() = default;

Result<std::unique_ptr<RealtimeClient>> RealtimeClient::open(const std::string& path,
                                                             const CallContext& ctx) {
    auto store = open_configuration(path, false, ctx);
    if (!core::is_ok(store)) return core::error_of(store);
    auto impl = std::make_unique<Impl>();
    impl->store = std::get<0>(std::move(store));
    return std::unique_ptr<RealtimeClient>(new RealtimeClient(std::move(impl)));
}

Result<std::unique_ptr<RealtimeClient>> RealtimeClient::initialize(const std::string& path,
                                                                   const CallContext& ctx) {
    auto store = open_configuration(path, true, ctx);
    if (!core::is_ok(store)) return core::error_of(store);
    auto impl = std::make_unique<Impl>();
    impl->store = std::get<0>(std::move(store));
    return std::unique_ptr<RealtimeClient>(new RealtimeClient(std::move(impl)));
}

Result<Record> RealtimeClient::read(const RecordKey& key, const CallContext& ctx) {
    return impl_->store->read(key, ctx);
}

Result<std::vector<RecordKey>> RealtimeClient::listAssets(const Scope& scope,
                                                          const CallContext& ctx) {
    return impl_->store->listAssets(scope, ctx);
}

Result<WriteReceipt> RealtimeClient::compareExchange(const WriteRequest& request,
                                                     const CallContext& ctx) {
    return impl_->store->compareExchange(request, ctx);
}

Result<EraseReceipt> RealtimeClient::erase(const EraseRequest& request, const CallContext& ctx) {
    return impl_->store->erase(request, ctx);
}

std::vector<Result<Record>> RealtimeClient::readMany(const std::vector<RecordKey>& keys,
                                                     const CallContext& ctx) {
    return impl_->store->readMany(keys, ctx);
}

std::vector<Result<WriteReceipt>> RealtimeClient::writeMany(
    const std::vector<WriteRequest>& requests, const CallContext& ctx) {
    return impl_->store->writeMany(requests, ctx);
}

BackendInfo RealtimeClient::info() const { return impl_->store->info(); }

void RealtimeClient::close() noexcept { impl_->store->close(); }
}  // namespace ems::rtdb

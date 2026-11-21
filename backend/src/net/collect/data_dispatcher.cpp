#include "data_dispatcher.h"

#include <rtdb/client/realtime_client.h>

namespace ems::net::collect {
DataDispatcher::DataDispatcher() = default;
DataDispatcher::~DataDispatcher() = default;

bool DataDispatcher::initialize(const DataDispatcherConfig& config) {
    auto opened = rtdb::RealtimeClient::open(
        config.rtdbConfigPath_,
        core::make_call_context(std::chrono::milliseconds(config.timeoutMs_)));
    if (!core::is_ok(opened)) return false;
    std::lock_guard lock(mutex_);
    config_ = config;
    initialized_ = true;
    return true;
}

bool DataDispatcher::configure(std::shared_ptr<const rtdb::RuntimeCatalog> catalog) {
    if (!catalog || catalog->scope().code != config_.rtdbScopeCode_) return false;
    const auto ctx = core::make_call_context(std::chrono::seconds(30));
    auto opened = rtdb::PointClient::open(config_.rtdbConfigPath_, std::move(catalog), ctx);
    if (!core::is_ok(opened)) return false;
    auto client = core::value_of(opened);
    if (!core::is_ok(client->installWriter(ctx))) return false;
    std::lock_guard lock(mutex_);
    client_ = std::move(client);
    return true;
}

void DataDispatcher::shutdown() {
    std::lock_guard lock(mutex_);
    client_.reset();
    initialized_ = false;
}

bool DataDispatcher::is_initialized() const {
    std::lock_guard lock(mutex_);
    return initialized_;
}

std::shared_ptr<rtdb::PointClient> DataDispatcher::point_client() const {
    std::lock_guard lock(mutex_);
    return client_;
}

bool DataDispatcher::is_connected() const { return point_client() != nullptr; }

int DataDispatcher::batch_update(const std::vector<PointValue>& results) {
    auto client = point_client();
    if (!client) return -1;
    int count = 0;
    for (const auto& sample : results) {
        const auto quality = sample.quality == 0 ? model::Quality::Good : model::Quality::Invalid;
        auto updated =
            client->update(sample.code, sample.value, quality, sample.timestamp,
                           core::make_call_context(std::chrono::milliseconds(config_.timeoutMs_)));
        const bool ok = core::is_ok(updated);
        if (ok) ++count;
        DispatchCallback callback;
        {
            std::lock_guard lock(mutex_);
            callback = callback_;
            ++stats_.totalUpdates;
            if (!ok) ++stats_.failedUpdates;
        }
        if (callback)
            callback((sample.details.is_object() ? sample.details.value("point_type", "yc") : "yc"),
                     sample.code, ok);
    }
    {
        std::lock_guard lock(mutex_);
        ++stats_.batchCount;
    }
    return count;
}

bool DataDispatcher::update_yc(const std::string& id, double value, int quality, uint64_t time) {
    PointValue sample;
    sample.code = id;
    sample.value = value;
    sample.quality = quality;
    sample.timestamp = time;
    sample.details = {{"point_type", "yc"}};
    return batch_update({sample}) == 1;
}

bool DataDispatcher::update_yx(const std::string& id, int value, int quality, uint64_t time) {
    PointValue sample;
    sample.code = id;
    sample.value = value;
    sample.quality = quality;
    sample.timestamp = time;
    sample.details = {{"point_type", "yx"}};
    return batch_update({sample}) == 1;
}

void DataDispatcher::set_dispatch_callback(DispatchCallback callback) {
    std::lock_guard lock(mutex_);
    callback_ = std::move(callback);
}

DataDispatcher::Stats DataDispatcher::stats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}
}  // namespace ems::net::collect

#include <rtdb/client/realtime_client.h>
#include <rtdb/client/rtdb.h>
#include <rtdb/model/rtdb_channel.h>

#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <thread>

namespace ems::rtdb {
namespace {
using Json = nlohmann::json;

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool matches(const std::string& pattern, const std::string& text) {
    std::size_t p = 0, t = 0, star = std::string::npos, retry = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            retry = t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++retry;
        } else
            return false;
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}
}  // namespace

struct RtDb::Impl {
    std::unique_ptr<RealtimeClient> client;
    RtDbConfig config;
    mutable std::recursive_mutex mutex;
    std::string error;
    uint64_t commands = 0;

    CallContext context() const {
        return core::make_call_context(std::chrono::milliseconds(config.timeoutMs));
    }

    RecordKey key(const std::string& logical) const {
        RecordKey out{
            {ScopeKind::Station, config.scope_code}, RecordKind::DeviceState, {"business"}, {}};
        const auto bytes = config.prefix + logical;
        constexpr char hex[] = "0123456789abcdef";
        for (std::size_t pos = 0; pos < bytes.size(); pos += 31) {
            std::string chunk = "k";
            for (std::size_t i = pos; i < bytes.size() && i < pos + 31; ++i) {
                const auto c = static_cast<unsigned char>(bytes[i]);
                chunk += hex[c >> 4];
                chunk += hex[c & 15];
            }
            out.device_path.push_back(std::move(chunk));
        }
        return out;
    }

    bool fail(const Error& failure) {
        error = failure.message;
        return false;
    }

    bool ready() {
        if (client) return true;
        error = "RTDB client is not initialized";
        return false;
    }

    bool expired(const Record& record) const {
        const auto& expiry = record.fields.at("expires_ms");
        return expiry != "0" && std::stoll(expiry) <= now_ms();
    }

    bool read(const std::string& logical, Json& payload, const std::string& type) {
        if (!ready()) return false;
        try {
            const auto ctx = context();
            const auto id = key(logical);
            auto result = client->read(id, ctx);
            ++commands;
            if (!core::is_ok(result)) return fail(core::error_of(result));
            const auto& record = core::value_of(result);
            if (record.fields.at("logical_key") != config.prefix + logical) {
                error = "business record identity mismatch";
                return false;
            }
            if (expired(record)) {
                client->erase({id, record.revision}, ctx);
                error = "business record expired";
                return false;
            }
            payload = Json::parse(record.fields.at("payload"));
            if (record.fields.at("schema") != "1" || payload.at("type") != type ||
                (type == "string" && !payload.at("value").is_string()) ||
                (type == "hash" && !payload.at("value").is_object())) {
                error = "business record type mismatch";
                return false;
            }
            if (type == "hash") {
                for (const auto& field : payload.at("value")) {
                    if (!field.is_string()) {
                        error = "invalid business hash field";
                        return false;
                    }
                }
            }
            return true;
        } catch (const std::exception&) {
            error = "invalid business record";
            return false;
        }
    }

    bool modify(const std::string& logical, const std::function<bool(Json&, int64_t&)>& change) {
        if (!ready()) return false;
        try {
            const auto ctx = context();
            const auto id = key(logical);
            for (int attempt = 0; attempt < 32 && !core::has_expired(ctx); ++attempt) {
                auto old = client->read(id, ctx);
                ++commands;
                std::optional<Revision> expected;
                Json payload;
                int64_t expiry = 0;
                if (core::is_ok(old)) {
                    const auto& record = core::value_of(old);
                    if (record.fields.at("logical_key") != config.prefix + logical) {
                        error = "business record identity mismatch";
                        return false;
                    }
                    expected = record.revision;
                    if (!expired(record)) {
                        payload = Json::parse(record.fields.at("payload"));
                        expiry = std::stoll(record.fields.at("expires_ms"));
                    }
                } else if (core::error_of(old).code != ErrorCode::NotFound) {
                    return fail(core::error_of(old));
                }
                if (!change(payload, expiry)) {
                    error = "business condition/type mismatch";
                    return false;
                }
                Fields fields{{"logical_key", config.prefix + logical},
                              {"payload", payload.dump()},
                              {"expires_ms", std::to_string(expiry)},
                              {"schema", "1"}};
                auto written = client->compareExchange({id, std::move(fields), expected}, ctx);
                ++commands;
                if (core::is_ok(written)) return true;
                const auto& failure = core::error_of(written);
                if (failure.effect != WriteEffect::NotApplied ||
                    (failure.code != ErrorCode::Conflict && failure.code != ErrorCode::NotFound))
                    return fail(failure);
                std::this_thread::yield();
            }
            error = "business update conflict/deadline exhausted";
            return false;
        } catch (const std::exception&) {
            error = "invalid business update";
            return false;
        }
    }

    bool erase(const std::string& logical, const std::optional<std::string>& owner = {}) {
        if (!ready()) return false;
        const auto ctx = context();
        const auto id = key(logical);
        auto current = client->read(id, ctx);
        ++commands;
        if (!core::is_ok(current)) {
            if (core::error_of(current).code == ErrorCode::NotFound) return true;
            return fail(core::error_of(current));
        }
        try {
            const auto& record = core::value_of(current);
            if (owner && !expired(record)) {
                const auto payload = Json::parse(record.fields.at("payload"));
                if (payload.at("type") != "string" || payload.at("value") != *owner) {
                    error = "lock owner mismatch";
                    return false;
                }
            }
            auto erased = client->erase({id, record.revision}, ctx);
            ++commands;
            if (!core::is_ok(erased)) return fail(core::error_of(erased));
            return true;
        } catch (const std::exception&) {
            error = "invalid business record";
            return false;
        }
    }
};

RtDb::RtDb() : impl_(std::make_unique<Impl>()) {}

RtDb::~RtDb() = default;

bool RtDb::initialize(const RtDbConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    impl_->client.reset();
    impl_->config = config;
    if (config.timeoutMs <= 0 ||
        !validate_record_key(
             {{ScopeKind::Station, config.scope_code}, RecordKind::DeviceState, {}, {}})
             .empty()) {
        impl_->error = "invalid business client configuration";
        return false;
    }
    auto opened = RealtimeClient::open(config.config_path, impl_->context());
    if (!core::is_ok(opened)) return impl_->fail(core::error_of(opened));
    impl_->client = std::get<0>(std::move(opened));
    return true;
}

void RtDb::shutdown() {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    impl_->client.reset();
}

bool RtDb::isConnected() const {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    return bool(impl_->client);
}

std::string RtDb::getLastError() const {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    return impl_->error;
}

uint64_t RtDb::getCommandCount() const {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    return impl_->commands;
}

bool RtDb::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    return impl_->modify(key, [&](Json& p, int64_t& expiry) {
        p = {{"type", "string"}, {"value", value}};
        expiry = 0;
        return true;
    });
}

bool RtDb::set(const std::string& key, double value) {
    return std::isfinite(value) && set(key, Json(value).dump());
}

bool RtDb::set(const std::string& key, int value) { return set(key, std::to_string(value)); }

bool RtDb::get(const std::string& key, std::string& value) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    Json p;
    if (!impl_->read(key, p, "string")) return false;
    value = p.at("value").get<std::string>();
    return true;
}

bool RtDb::get(const std::string& key, double& value) {
    std::string text;
    if (!get(key, text)) return false;
    try {
        std::size_t n = 0;
        const auto parsed = std::stod(text, &n);
        if (n != text.size() || !std::isfinite(parsed)) return false;
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool RtDb::get(const std::string& key, int& value) {
    std::string text;
    if (!get(key, text)) return false;
    try {
        std::size_t n = 0;
        const auto parsed = std::stoi(text, &n);
        if (n != text.size()) return false;
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool RtDb::setHash(const std::string& key, const std::map<std::string, std::string>& fields) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    return impl_->modify(key, [&](Json& p, int64_t&) {
        if (p.is_null()) p = {{"type", "hash"}, {"value", Json::object()}};
        if (p.at("type") != "hash") return false;
        for (const auto& field : fields) p["value"][field.first] = field.second;
        return true;
    });
}

bool RtDb::getHash(const std::string& key, std::map<std::string, std::string>& fields) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    Json p;
    if (!impl_->read(key, p, "hash")) return false;
    try {
        auto copy = p.at("value").get<Fields>();
        fields = std::move(copy);
        return true;
    } catch (...) {
        impl_->error = "invalid hash fields";
        return false;
    }
}

bool RtDb::replaceHash(const std::string& key, const Fields& fields, int seconds) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (seconds < 0) return false;
    return impl_->modify(key, [&](Json& payload, int64_t& expiry) {
        payload = {{"type", "hash"}, {"value", fields}};
        expiry = seconds == 0 ? 0 : now_ms() + int64_t(seconds) * 1000;
        return true;
    });
}

bool RtDb::setHashField(const std::string& key, const std::string& field,
                        const std::string& value) {
    return setHash(key, {{field, value}});
}

bool RtDb::deleteKey(const std::string& key) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    return impl_->erase(key);
}

bool RtDb::exists(const std::string& key) {
    Fields fields;
    std::string value;
    return getHash(key, fields) || get(key, value);
}

std::vector<std::string> RtDb::keys(const std::string& pattern) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    std::vector<std::string> out;
    if (!impl_->ready()) return out;
    const auto ctx = impl_->context();
    auto listed =
        impl_->client->listDeviceStates({ScopeKind::Station, impl_->config.scope_code}, ctx);
    if (!core::is_ok(listed)) {
        impl_->fail(core::error_of(listed));
        return {};
    }
    for (const auto& id : core::value_of(listed)) {
        if (id.device_path.empty() || id.device_path.front() != "business") continue;
        auto record = impl_->client->read(id, ctx);
        if (!core::is_ok(record)) {
            if (core::error_of(record).code == ErrorCode::NotFound) continue;
            impl_->fail(core::error_of(record));
            return {};
        }
        try {
            const auto& r = core::value_of(record);
            const auto& full = r.fields.at("logical_key");
            if (full.compare(0, impl_->config.prefix.size(), impl_->config.prefix) != 0) continue;
            if (impl_->expired(r)) {
                impl_->client->erase({id, r.revision}, ctx);
                continue;
            }
            const auto logical = full.substr(impl_->config.prefix.size());
            if (matches(pattern, logical)) out.push_back(logical);
        } catch (const std::exception&) {
            impl_->error = "invalid business catalog record";
            return {};
        }
    }
    return out;
}

bool RtDb::setWithExpire(const std::string& key, const std::string& value, int seconds) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (seconds <= 0) return false;
    return impl_->modify(key, [&](Json& p, int64_t& expiry) {
        p = {{"type", "string"}, {"value", value}};
        expiry = now_ms() + int64_t(seconds) * 1000;
        return true;
    });
}

bool RtDb::expire(const std::string& key, int seconds) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (seconds <= 0) return deleteKey(key);
    return impl_->modify(key, [&](Json& p, int64_t& expiry) {
        if (p.is_null()) return false;
        expiry = now_ms() + int64_t(seconds) * 1000;
        return true;
    });
}

bool RtDb::append(const std::string& key, const std::string& suffix) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    return impl_->modify(key, [&suffix](Json& p, int64_t&) {
        if (p.is_null()) p = {{"type", "string"}, {"value", ""}};
        if (p.at("type") != "string") return false;
        const std::string previous = p.at("value").get<std::string>();
        p["value"] = previous + suffix;
        return true;
    });
}

bool RtDb::incr(const std::string& key, int64_t& result) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    int64_t next = 0;
    const bool ok = impl_->modify(key, [&](Json& p, int64_t&) {
        if (p.is_null()) p = {{"type", "string"}, {"value", "0"}};
        if (p.at("type") != "string") return false;
        const auto value = p.at("value").get<std::string>();
        std::size_t n = 0;
        const auto old = std::stoll(value, &n);
        if (n != value.size() || old == std::numeric_limits<int64_t>::max()) return false;
        next = old + 1;
        p["value"] = std::to_string(next);
        return true;
    });
    if (ok) result = next;
    return ok;
}

bool RtDb::incrByFloat(const std::string& key, double value, double& result) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (!std::isfinite(value)) return false;
    double next = 0;
    const bool ok = impl_->modify(key, [&](Json& p, int64_t&) {
        if (p.is_null()) p = {{"type", "string"}, {"value", "0"}};
        if (p.at("type") != "string") return false;
        const auto text = p.at("value").get<std::string>();
        std::size_t n = 0;
        next = std::stod(text, &n) + value;
        if (n != text.size() || !std::isfinite(next)) return false;
        p["value"] = Json(next).dump();
        return true;
    });
    if (ok) result = next;
    return ok;
}

bool RtDb::setIfAbsentWithExpire(const std::string& key, const std::string& value, int seconds) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (seconds <= 0) return false;
    return impl_->modify(key, [&](Json& p, int64_t& expiry) {
        if (!p.is_null()) return false;
        p = {{"type", "string"}, {"value", value}};
        expiry = now_ms() + int64_t(seconds) * 1000;
        return true;
    });
}

bool RtDb::deleteIfValue(const std::string& key, const std::string& value) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    return impl_->erase(key, value);
}

std::future<bool> RtDb::asyncSet(const std::string& key, const std::string& value) {
    return std::async(std::launch::async, [this, key, value] { return set(key, value); });
}

std::future<std::string> RtDb::asyncGet(const std::string& key) {
    return std::async(std::launch::async, [this, key] {
        std::string value;
        get(key, value);
        return value;
    });
}

bool RtDb::setYc(const std::string& code, const YcPoint& point) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    try {
        auto copy = point;
        copy.code_ = code;
        const Json fields = {{"json", copy.to_json().dump()}};
        return impl_->modify("rtdb:yc:" + code, [&](Json& payload, int64_t& expiry) {
            payload = {{"type", "hash"}, {"value", fields}};
            expiry = 0;
            return true;
        });
    } catch (const std::exception&) {
        impl_->error = "invalid point value";
        return false;
    }
}

bool RtDb::getYc(const std::string& code, YcPoint& point) {
    Fields fields;
    if (!getHash("rtdb:yc:" + code, fields)) return false;
    try {
        auto copy = YcPoint{};
        copy.from_json(Json::parse(fields.at("json")));
        copy.code_ = code;
        point = std::move(copy);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool RtDb::deleteYc(const std::string& code) { return deleteKey("rtdb:yc:" + code); }

std::vector<std::string> RtDb::getYcCodes(const std::string& pattern) {
    auto found = keys("rtdb:yc:" + pattern);
    for (auto& code : found) code.erase(0, 8);
    return found;
}

std::vector<YcPoint> RtDb::getYcPoints(const std::string& pattern) {
    std::vector<YcPoint> out;
    for (const auto& code : getYcCodes(pattern)) {
        YcPoint point;
        if (getYc(code, point)) out.push_back(std::move(point));
    }
    return out;
}

bool RtDb::batchSetYc(const std::map<std::string, YcPoint>& points) {
    bool ok = true;
    for (const auto& item : points) {
        const bool written = setYc(item.first, item.second);
        ok = written && ok;
    }
    return ok;
}

bool RtDb::batchGetYc(const std::vector<std::string>& codes, std::vector<YcPoint>& points) {
    std::vector<YcPoint> out;
    for (const auto& code : codes) {
        YcPoint point;
        if (!getYc(code, point)) return false;
        out.push_back(std::move(point));
    }
    points = std::move(out);
    return true;
}

bool RtDb::setYx(const std::string& code, const YxPoint& point) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    try {
        auto copy = point;
        copy.code_ = code;
        const Json fields = {{"json", copy.to_json().dump()}};
        return impl_->modify("rtdb:yx:" + code, [&](Json& payload, int64_t& expiry) {
            payload = {{"type", "hash"}, {"value", fields}};
            expiry = 0;
            return true;
        });
    } catch (const std::exception&) {
        impl_->error = "invalid point value";
        return false;
    }
}

bool RtDb::getYx(const std::string& code, YxPoint& point) {
    Fields fields;
    if (!getHash("rtdb:yx:" + code, fields)) return false;
    try {
        auto copy = YxPoint{};
        copy.from_json(Json::parse(fields.at("json")));
        copy.code_ = code;
        point = std::move(copy);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool RtDb::deleteYx(const std::string& code) { return deleteKey("rtdb:yx:" + code); }

std::vector<std::string> RtDb::getYxCodes(const std::string& pattern) {
    auto found = keys("rtdb:yx:" + pattern);
    for (auto& code : found) code.erase(0, 8);
    return found;
}

std::vector<YxPoint> RtDb::getYxPoints(const std::string& pattern) {
    std::vector<YxPoint> out;
    for (const auto& code : getYxCodes(pattern)) {
        YxPoint point;
        if (getYx(code, point)) out.push_back(std::move(point));
    }
    return out;
}

bool RtDb::batchSetYx(const std::map<std::string, YxPoint>& points) {
    bool ok = true;
    for (const auto& item : points) {
        const bool written = setYx(item.first, item.second);
        ok = written && ok;
    }
    return ok;
}

bool RtDb::batchGetYx(const std::vector<std::string>& codes, std::vector<YxPoint>& points) {
    std::vector<YxPoint> out;
    for (const auto& code : codes) {
        YxPoint point;
        if (!getYx(code, point)) return false;
        out.push_back(std::move(point));
    }
    points = std::move(out);
    return true;
}

bool RtDb::setYk(const std::string& code, const YkPoint& point) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    try {
        auto copy = point;
        copy.code_ = code;
        const Json fields = {{"json", copy.to_json().dump()}};
        return impl_->modify("rtdb:yk:" + code, [&](Json& payload, int64_t& expiry) {
            payload = {{"type", "hash"}, {"value", fields}};
            expiry = 0;
            return true;
        });
    } catch (const std::exception&) {
        impl_->error = "invalid point value";
        return false;
    }
}

bool RtDb::getYk(const std::string& code, YkPoint& point) {
    Fields fields;
    if (!getHash("rtdb:yk:" + code, fields)) return false;
    try {
        auto copy = YkPoint{};
        copy.from_json(Json::parse(fields.at("json")));
        copy.code_ = code;
        point = std::move(copy);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool RtDb::deleteYk(const std::string& code) { return deleteKey("rtdb:yk:" + code); }

std::vector<std::string> RtDb::getYkCodes(const std::string& pattern) {
    auto found = keys("rtdb:yk:" + pattern);
    for (auto& code : found) code.erase(0, 8);
    return found;
}

std::vector<YkPoint> RtDb::getYkPoints(const std::string& pattern) {
    std::vector<YkPoint> out;
    for (const auto& code : getYkCodes(pattern)) {
        YkPoint point;
        if (getYk(code, point)) out.push_back(std::move(point));
    }
    return out;
}

bool RtDb::batchSetYk(const std::map<std::string, YkPoint>& points) {
    bool ok = true;
    for (const auto& item : points) {
        const bool written = setYk(item.first, item.second);
        ok = written && ok;
    }
    return ok;
}

bool RtDb::batchGetYk(const std::vector<std::string>& codes, std::vector<YkPoint>& points) {
    std::vector<YkPoint> out;
    for (const auto& code : codes) {
        YkPoint point;
        if (!getYk(code, point)) return false;
        out.push_back(std::move(point));
    }
    points = std::move(out);
    return true;
}

bool RtDb::setYt(const std::string& code, const YtPoint& point) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    try {
        auto copy = point;
        copy.code_ = code;
        const Json fields = {{"json", copy.to_json().dump()}};
        return impl_->modify("rtdb:yt:" + code, [&](Json& payload, int64_t& expiry) {
            payload = {{"type", "hash"}, {"value", fields}};
            expiry = 0;
            return true;
        });
    } catch (const std::exception&) {
        impl_->error = "invalid point value";
        return false;
    }
}

bool RtDb::getYt(const std::string& code, YtPoint& point) {
    Fields fields;
    if (!getHash("rtdb:yt:" + code, fields)) return false;
    try {
        auto copy = YtPoint{};
        copy.from_json(Json::parse(fields.at("json")));
        copy.code_ = code;
        point = std::move(copy);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool RtDb::deleteYt(const std::string& code) { return deleteKey("rtdb:yt:" + code); }

std::vector<std::string> RtDb::getYtCodes(const std::string& pattern) {
    auto found = keys("rtdb:yt:" + pattern);
    for (auto& code : found) code.erase(0, 8);
    return found;
}

std::vector<YtPoint> RtDb::getYtPoints(const std::string& pattern) {
    std::vector<YtPoint> out;
    for (const auto& code : getYtCodes(pattern)) {
        YtPoint point;
        if (getYt(code, point)) out.push_back(std::move(point));
    }
    return out;
}

bool RtDb::batchSetYt(const std::map<std::string, YtPoint>& points) {
    bool ok = true;
    for (const auto& item : points) {
        const bool written = setYt(item.first, item.second);
        ok = written && ok;
    }
    return ok;
}

bool RtDb::batchGetYt(const std::vector<std::string>& codes, std::vector<YtPoint>& points) {
    std::vector<YtPoint> out;
    for (const auto& code : codes) {
        YtPoint point;
        if (!getYt(code, point)) return false;
        out.push_back(std::move(point));
    }
    points = std::move(out);
    return true;
}

const char* protocolTypeToString(model::ProtocolType t) {
    switch (t) {
        case model::ProtocolType::ModbusRTU:
            return "ModbusRTU";
        case model::ProtocolType::ModbusTCP:
            return "ModbusTCP";
        case model::ProtocolType::IEC104:
            return "IEC104";
        case model::ProtocolType::DLT645:
            return "DLT645";
        case model::ProtocolType::DLT698:
            return "DLT698";
        case model::ProtocolType::IEC61850:
            return "IEC61850";
        case model::ProtocolType::OPCUA:
            return "OPCUA";
        case model::ProtocolType::MQTT:
            return "MQTT";
        case model::ProtocolType::CAN:
            return "CAN";
        case model::ProtocolType::Other:
            return "Other";
    }
    return "Unknown";
}

std::optional<model::ProtocolType> stringToProtocolType(const std::string& s) {
    if (s == "ModbusRTU") return model::ProtocolType::ModbusRTU;
    if (s == "ModbusTCP") return model::ProtocolType::ModbusTCP;
    if (s == "IEC104") return model::ProtocolType::IEC104;
    if (s == "DLT645") return model::ProtocolType::DLT645;
    if (s == "DLT698") return model::ProtocolType::DLT698;
    if (s == "IEC61850") return model::ProtocolType::IEC61850;
    if (s == "OPCUA") return model::ProtocolType::OPCUA;
    if (s == "MQTT") return model::ProtocolType::MQTT;
    if (s == "CAN") return model::ProtocolType::CAN;
    return std::nullopt;
}

std::unique_ptr<RtDb> createRtDb(const RtDbConfig& config) {
    auto client = std::make_unique<RtDb>();
    if (!client->initialize(config)) return nullptr;
    return client;
}
}  // namespace ems::rtdb

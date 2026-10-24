#include "json.h"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace ems::web {
ApiError::ApiError(int s, std::string c, std::string r, std::string e, Json d)
    : std::runtime_error(r),
      status(s),
      code(std::move(c)),
      reason(std::move(r)),
      effect(std::move(e)),
      details(std::move(d)) {}

void fields(const Json& j, std::initializer_list<const char*> allowed) {
    if (!j.is_object()) throw ApiError(400, "InvalidArgument", "object_required");
    for (auto it = j.begin(); it != j.end(); ++it) {
        bool ok = false;
        for (auto name : allowed)
            if (it.key() == name) ok = true;
        if (!ok)
            throw ApiError(400, "InvalidArgument", "unknown_field", "NotApplied",
                           Json::array({{{"field", it.key()}}}));
    }
}

std::string text(const Json& j, const char* key, std::size_t max) {
    if (!j.count(key) || !j.at(key).is_string())
        throw ApiError(400, "InvalidArgument", "string_required", "NotApplied",
                       Json::array({{{"field", key}}}));
    auto value = j.at(key).get<std::string>();
    if (value.empty() || value.size() > max || value.find('\0') != std::string::npos)
        throw ApiError(400, "InvalidArgument", "invalid_string");
    return value;
}

void fail(const core::Error& e) {
    using E = core::ErrorCode;
    int status = 500;
    switch (e.code) {
        case E::InvalidArgument:
            status = 422;
            break;
        case E::NotFound:
            status = 404;
            break;
        case E::Conflict:
            status = 409;
            break;
        case E::PermissionDenied:
            status = 403;
            break;
        case E::NotReady:
        case E::Unavailable:
        case E::ResourceExhausted:
            status = 503;
            break;
        case E::DeadlineExceeded:
            status = 504;
            break;
        case E::Unsupported:
            status = 422;
            break;
        default:
            break;
    }
    throw ApiError(status, core::to_string(e.code), "database_operation_failed",
                   core::to_string(e.effect));
}

std::string utc_now() {
    auto now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
}  // namespace ems::web

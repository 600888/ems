#include "health_service.h"

namespace ems::web {
void HealthService::set_ready(bool value) { ready_ = value; }

bool HealthService::ready() const { return ready_.load(); }

void HealthService::record(int status, std::uint64_t elapsed_us) {
    ++completed_;
    elapsed_us_ += elapsed_us;
    if (status >= 400) ++errors_;
    if (status == 429 || status == 503) ++rejected_;
}

Json HealthService::metrics() const {
    // 指标只含计数和资源表现，不包含账号、对象内容或上传文件。
    const auto count = completed_.load();
    return {{"ready", ready()},
            {"requests_total", std::to_string(count)},
            {"errors_total", std::to_string(errors_.load())},
            {"rejected_total", std::to_string(rejected_.load())},
            {"average_latency_us",
             count ? Json(static_cast<double>(elapsed_us_.load()) / count) : Json(nullptr)}};
}
}  // namespace ems::web

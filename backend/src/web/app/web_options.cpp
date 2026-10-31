#include "web_options.h"

#include <filesystem>
#include <fstream>

namespace ems::web {
WebOptions load_web_options(const std::string& path) {
    if (std::filesystem::file_size(std::filesystem::u8path(path)) > 1024 * 1024)
        throw std::invalid_argument("web config too large");
    std::ifstream input(std::filesystem::u8path(path));
    Json j;
    input >> j;
    fields(j, {"host", "port", "allowed_origin", "database_config", "identity_key_file",
               "document_root", "workspace", "io_threads", "workers", "queue_size",
               "import_workers", "import_queue_size", "request_timeout_ms", "import_timeout_ms",
               "audit_retention_days", "secure_cookie"});
    WebOptions o;
    auto number = [&](const char* key, std::size_t fallback, std::size_t maximum) {
        if (j.count(key) && !j.at(key).is_number_integer())
            throw std::invalid_argument("integer configuration required");
        auto value = j.value(key, static_cast<std::int64_t>(fallback));
        if (value <= 0 || static_cast<std::uint64_t>(value) > maximum)
            throw std::invalid_argument("web configuration bounds");
        return static_cast<std::size_t>(value);
    };

    o.host = j.value("host", o.host);
    o.port = static_cast<unsigned short>(number("port", 8080, 65535));
    o.allowed_origin =
        j.value("allowed_origin", std::string("http://") + o.host + ":" + std::to_string(o.port));
    o.database_config = j.value("database_config", std::string{});
    o.identity_key_file = j.value("identity_key_file", std::string{});
    o.document_root = j.value("document_root", std::string{});
    o.workspace = j.value("workspace", o.workspace);
    o.io_threads = number("io_threads", 2, 64);
    o.workers = number("workers", 4, 128);
    o.queue_size = number("queue_size", 128, 10000);
    o.import_workers = number("import_workers", 1, 8);
    o.import_queue_size = number("import_queue_size", 8, 128);
    o.request_timeout_ms = static_cast<int>(number("request_timeout_ms", 5000, 60000));
    o.import_timeout_ms = static_cast<int>(number("import_timeout_ms", 30000, 120000));
    o.audit_retention_days = static_cast<int>(number("audit_retention_days", 180, 3650));
    o.secure_cookie = j.value("secure_cookie", false);

    // 仅提供本机明文监听或上游 TLS 终止部署，不能误将远程会话以非安全 cookie 发布。
    if (o.host.empty() || o.allowed_origin.empty() ||
        (o.secure_cookie && o.allowed_origin.rfind("https://", 0) != 0) ||
        ((o.host != "127.0.0.1" && o.host != "::1") && !o.secure_cookie))
        throw std::invalid_argument("remote listener requires TLS proxy and secure cookie");
    return o;
}
}  // namespace ems::web

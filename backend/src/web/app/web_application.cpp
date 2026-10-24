#include <drogon/drogon.h>

#include <atomic>
#include <filesystem>
#include <iostream>
#include <unordered_map>

#include "module_registry.h"
#include "web/modules/device/application/device_service.h"
#include "web/modules/health/api/health_service.h"
#include "web/modules/identity/application/session_service.h"
#include "web/platform/crypto.h"
#include "web/platform/executor/bounded_executor.h"
#include "web_options.h"

namespace ems::web {
namespace {
using Callback = std::function<void(const drogon::HttpResponsePtr&)>;

struct Reply {
    Callback callback;
    std::atomic<bool> finished{false};
    std::atomic<bool> started{false};
    std::string trace;
    std::shared_ptr<HealthService> health;
    std::chrono::steady_clock::time_point accepted = std::chrono::steady_clock::now();

    void send(int status, const Json& body, const std::string& cookie = {}) {
        if (finished.exchange(true)) return;
        if (health)
            health->record(status, static_cast<std::uint64_t>(
                                       std::chrono::duration_cast<std::chrono::microseconds>(
                                           std::chrono::steady_clock::now() - accepted)
                                           .count()));
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setStatusCode(static_cast<drogon::HttpStatusCode>(status));
        response->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        // 响应体由 nlohmann 序列化，避免业务代码使用 JsonCpp 接口。
        response->setBody(body.dump());
        response->addHeader("Cache-Control", "no-store");
        response->addHeader("X-Request-ID", trace);
        response->addHeader("X-Content-Type-Options", "nosniff");
        if (status == 429 || status == 503) response->addHeader("Retry-After", "1");
        if (!cookie.empty()) response->addHeader("Set-Cookie", cookie);
        // 回复后立即释放框架回调；它可能持有请求对象，不能被期限定时器留到 I/O 退出之后。
        auto once = std::move(callback);
        once(response);
    }

    void error(const ApiError& error) {
        send(error.status, {{"error",
                             {{"code", error.code},
                              {"reason", error.reason},
                              {"message", error.reason},
                              {"effect", error.effect},
                              {"details", error.details}}},
                            {"meta", {{"request_id", trace}}}});
    }
};

struct State {
    WebOptions options;
    SessionService identity;
    DeviceService device;
    BoundedExecutor business;
    BoundedExecutor imports;
    std::shared_ptr<HealthService> health = std::make_shared<HealthService>();

    State(const WebOptions& o, database::MySqlDatabaseContext database)
        : options(o),
          identity(o.users, o.session_capacity, o.session_ttl_seconds),
          device(std::move(database), o.workspace),
          business(o.workers, o.queue_size),
          imports(o.import_workers, o.import_queue_size) {}
};

Json session_json(const Session& session) {
    return {{"subject_id", session.principal.subject_id},
            {"permissions", session.principal.permissions},
            {"station_ids", session.principal.station_ids},
            {"device_roots", session.principal.device_roots},
            {"csrf_token", session.csrf}};
}

void dispatch(const std::shared_ptr<State>& state, const drogon::HttpRequestPtr& request,
              Callback callback, bool post) {
    auto reply = std::make_shared<Reply>();
    reply->callback = std::move(callback);
    reply->trace = random_token();
    reply->health = state->health;
    const auto path = request->path();
    const bool upload =
        path == "/api/point-imports/create" || path == "/api/station-imports/create";
    if (!upload && request->body().size() > 1024 * 1024) {
        reply->error(ApiError(413, "ResourceExhausted", "json_size"));
        return;
    }
    const auto budget =
        upload ? state->options.import_timeout_ms : state->options.request_timeout_ms;
    const auto context = core::make_call_context(std::chrono::milliseconds(budget), reply->trace);
    const auto action = path.substr(path.find_last_of('/') + 1);
    const bool write = action == "create" || action == "update" || action == "delete" ||
                       action == "apply" || action == "batch-change";

    // 完成与期限竞争只回复一次；开始后的写超时不能宣称确定未保存。
    std::weak_ptr<Reply> weak_reply = reply;
    drogon::app().getLoop()->runAfter(static_cast<double>(budget) / 1000, [weak_reply, write] {
        auto reply = weak_reply.lock();
        if (!reply) return;
        reply->error(ApiError(
            504, "DeadlineExceeded", "deadline_exceeded",
            write && reply->started ? "Unknown" : (write ? "NotApplied" : "NotApplicable")));
    });

    auto task = [state, request, reply, post, path, upload, context] {
        if (reply->finished || core::has_expired(context)) return;
        reply->started = true;
        try {
            if (!request->query().empty())
                throw ApiError(400, "InvalidArgument", "url_parameters_not_allowed");
            if (!post && !request->body().empty())
                throw ApiError(400, "InvalidArgument", "get_body_not_allowed");
            if (post && request->getHeader("origin") != state->options.allowed_origin)
                throw ApiError(403, "PermissionDenied", "origin_not_allowed");
            Json body = Json::object();
            Upload file;

            if (upload) {
                drogon::MultiPartParser parser;
                if (parser.parse(request) != 0)
                    throw ApiError(400, "InvalidArgument", "invalid_multipart");
                const auto& parameters = parser.getParameters();
                if (parameters.size() != 1 || !parameters.count("metadata") ||
                    parameters.at("metadata").size() > 64 * 1024 || parser.getFiles().size() != 1)
                    throw ApiError(400, "InvalidArgument", "multipart_file_and_metadata_required");
                const auto& part = parser.getFiles().front();
                if (part.getItemName() != "file" ||
                    part.fileLength() > (path == "/api/station-imports/create" ? 64 * 1024 * 1024
                                                                               : 20 * 1024 * 1024))
                    throw ApiError(413, "ResourceExhausted", "upload_size");
                body = Json::parse(parameters.at("metadata"));
                file.filename = part.getFileName();
                file.bytes = std::string(part.fileContent());
            } else if (post) {
                if (request->getHeader("content-type").rfind("application/json", 0) != 0)
                    throw ApiError(400, "InvalidArgument", "json_content_type_required");
                if (request->body().size() > 1024 * 1024)
                    throw ApiError(413, "ResourceExhausted", "json_size");
                body = Json::parse(request->body().begin(), request->body().end());
            }
            if (!body.is_object()) throw ApiError(400, "InvalidArgument", "object_required");

            if (path == "/health/live") {
                reply->send(200,
                            {{"data", {{"live", true}}}, {"meta", {{"request_id", reply->trace}}}});
                return;
            }
            if (path == "/health/ready") {
                if (!state->health->ready()) throw ApiError(503, "NotReady", "process_not_ready");
                state->device.check_schema(context);
                reply->send(
                    200, {{"data", {{"ready", true}}}, {"meta", {{"request_id", reply->trace}}}});
                return;
            }
            if (path == "/api/auth/login") {
                auto login = state->identity.login(body);
                auto cookie = "ems_session=" + login.first + "; Path=/; HttpOnly; SameSite=Strict" +
                              (state->options.secure_cookie ? "; Secure" : "");
                reply->send(200,
                            {{"data", session_json(login.second)},
                             {"meta", {{"request_id", reply->trace}}}},
                            cookie);
                return;
            }

            auto token = request->getCookie("ems_session");
            auto session = state->identity.get(token);
            if (post && !secure_equal(request->getHeader("x-csrf-token"), session.csrf))
                throw ApiError(403, "PermissionDenied", "csrf_mismatch");
            if (path == "/api/auth/session") {
                reply->send(200, {{"data", session_json(session)},
                                  {"meta", {{"request_id", reply->trace}}}});
                return;
            }
            if (path == "/api/system/metrics") {
                session.principal.require("system.read");
                reply->send(200, {{"data", state->health->metrics()},
                                  {"meta", {{"request_id", reply->trace}}}});
                return;
            }
            if (path == "/api/auth/logout") {
                fields(body, {});
                state->identity.logout(token);
                reply->send(
                    200,
                    {{"data", {{"logged_out", true}}}, {"meta", {{"request_id", reply->trace}}}},
                    "ems_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
                return;
            }
            if (!state->health->ready()) throw ApiError(503, "NotReady", "process_not_ready");
            auto result = state->device.execute(path, body, session.principal, context, file);
            result.meta["request_id"] = reply->trace;
            reply->send(result.status, {{"data", result.data}, {"meta", result.meta}});
        } catch (const ApiError& error) {
            reply->error(error);
        } catch (const Json::exception&) {
            reply->error(ApiError(400, "InvalidArgument", "invalid_json_or_field_type"));
        } catch (const std::exception&) {
            LOG_ERROR << "ems-web request failed, request_id=" << reply->trace;
            reply->error(
                ApiError(500, "Internal", "request_failed", post ? "Unknown" : "NotApplicable"));
        }
    };

    auto& executor = upload ? state->imports : state->business;
    if (!executor.submit(std::move(task)))
        reply->error(ApiError(503, "ResourceExhausted", "queue_full"));
}
}  // namespace

int run_web(const WebOptions& options) {
    auto mysql = take(database::load_mysql_options(options.database_config));
    mysql.require_configuration_schema = false;
    auto database = take(
        database::connect_mysql_database(mysql, core::make_call_context(std::chrono::seconds(15))));
    return run_web_with_database(options, std::move(database));
}

int run_web_with_database(const WebOptions& options, database::MySqlDatabaseContext database) {
    auto state = std::make_shared<State>(options, std::move(database));
    state->device.check_schema(core::make_call_context(std::chrono::seconds(15)));
    std::filesystem::create_directories(std::filesystem::u8path(options.workspace));

    auto& app = drogon::app();
    app.addListener(options.host, options.port)
        .setThreadNum(options.io_threads)
        .setClientMaxBodySize(21 * 1024 * 1024)
        .setClientMaxMemoryBodySize(21 * 1024 * 1024)
        .setUploadPath(options.workspace);
    if (!options.document_root.empty()) app.setDocumentRoot(options.document_root);

    // 在路由前统一拒绝多余方法；未匹配 API 始终返回 JSON 404。
    app.registerPreRoutingAdvice([](const drogon::HttpRequestPtr& request,
                                    drogon::AdviceCallback&& reject,
                                    drogon::AdviceChainCallback&& next) {
        bool mismatch = request->method() != drogon::Get && request->method() != drogon::Post;
        for (const auto& route : device_routes()) {
            if (request->path() == route.path && (request->method() == drogon::Post) != route.post)
                mismatch = true;
        }
        if (mismatch) {
            auto reply = std::make_shared<Reply>();
            reply->callback = std::move(reject);
            reply->trace = random_token();
            reply->error(ApiError(405, "Unsupported", "method_not_allowed"));
            return;
        }
        next();
    });
    for (const auto& route : device_routes()) {
        app.registerHandler(
            route.path,
            [state, post = route.post](const drogon::HttpRequestPtr& request, Callback&& callback) {
                dispatch(state, request, std::move(callback), post);
            },
            {route.post ? drogon::Post : drogon::Get});
    }
    for (const auto& route : std::vector<Route>{{"/api/auth/login", true},
                                                {"/api/auth/logout", true},
                                                {"/api/auth/session", false},
                                                {"/api/system/metrics", false},
                                                {"/health/live", false},
                                                {"/health/ready", false}}) {
        app.registerHandler(
            route.path,
            [state, post = route.post](const drogon::HttpRequestPtr& request, Callback&& callback) {
                dispatch(state, request, std::move(callback), post);
            },
            {route.post ? drogon::Post : drogon::Get});
    }
    app.setDefaultHandler([](const drogon::HttpRequestPtr&, Callback&& callback) {
        auto reply = std::make_shared<Reply>();
        reply->callback = std::move(callback);
        reply->trace = random_token();
        reply->error(ApiError(404, "NotFound", "route_not_found", "NotApplicable"));
    });

    auto closing = std::make_shared<std::atomic<bool>>(false);
    auto shutdown = [state, closing] {
        if (closing->exchange(true)) return;
        LOG_INFO << "ems-web stopping";
        state->health->set_ready(false);
        state->business.stop();
        state->imports.stop();

        // 保留 I/O 循环直到工作任务释放请求和事务，不从信号线程销毁在途连接。
        drogon::app().getLoop()->runEvery(0.05, [state] {
            if (state->business.idle() && state->imports.idle()) drogon::app().quit();
        });
    };
    app.setTermSignalHandler(shutdown).setIntSignalHandler(shutdown);
    state->health->set_ready(true);
    app.registerBeginningAdvice([host = options.host, port = options.port] {
        LOG_INFO << "ems-web ready, listen=" << host << ':' << port;
    });
    app.run();
    state->health->set_ready(false);
    state->business.stop();
    state->imports.stop();
    state->business.wait();
    state->imports.wait();
    return 0;
}
}  // namespace ems::web

#include <iostream>

#include "log/default_logger.hpp"
#include "log/process_logger.hpp"
#include "web/app/web_options.h"
#include "web/modules/identity/module.h"
#include "web/platform/crypto.h"
#include "web/platform/host_credentials.h"
#include "web/web_log.h"

int main(int argc, char** argv) {
    const ProcessLogger logger("ems-web");
    ems::web::configure_framework_logging();
    try {
        std::string config = "conf/web.json";
        bool check = false;
        std::string host_action;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--check-config")
                check = true;
            else if (arg == "--config" && i + 1 < argc)
                config = argv[++i];
            else if (arg == "--init-admin" || arg == "--recover-admin" ||
                     arg == "--create-identity-key" || arg == "--check-identity") {
                if (!host_action.empty()) throw std::invalid_argument("one host action required");
                host_action = arg;
            } else if (arg == "--help") {
                std::cout << "ems-web [--config path] [--check-config | --create-identity-key | "
                             "--check-identity | --init-admin | --recover-admin]\n";
                return 0;
            } else
                throw std::invalid_argument("unknown argument");
        }

        LOG_INFO("ems-web loading configuration: {}", config);
        const auto options = ems::web::load_web_options(config);
        if (check && !host_action.empty()) throw std::invalid_argument("one host action required");
        if (check) {
            LOG_INFO("ems-web configuration valid");
            std::cout << "Web configuration valid\n";
            return 0;
        }
        if (!host_action.empty()) {
            using namespace ems::web;
            if (host_action == "--create-identity-key") {
                auto path = options.identity_key_file;
                if (path.empty()) {
                    auto env = std::getenv("EMS_WEB_EDIT_KEY_FILE");
                    if (env) path = env;
                }
                create_identity_key(path);
                std::cout << "Identity key created\n";
                return 0;
            }
            auto call =
                IdentityCall{ems::core::make_call_context(std::chrono::seconds(30), random_token()),
                             "", "host-console"};
            auto mysql = take(ems::database::load_mysql_options(options.database_config));
            auto db = take(ems::database::connect_mysql_database(mysql, call.context));
            auto authorization = std::make_shared<AuthorizationService>(
                db, load_identity_key(options.identity_key_file));
            authorization->check_schema(call.context);
            if (host_action == "--check-identity") {
                std::cout << "Identity schema and key valid\n";
                return 0;
            }
            std::string username, name;
            username = read_host_line("Username: ");
            if (host_action == "--init-admin")
                name = read_host_line("Display name: ");
            else
                name = username;
            auto password = read_host_password("Temporary password: ");
            auto confirm = read_host_password("Confirm password: ");
            if (!secure_equal(password, confirm))
                throw std::invalid_argument("password confirmation");
            // 人工输入时间不占数据库调用预算；服务派生仍在任何行锁之外执行。
            call.context = ems::core::make_call_context(std::chrono::seconds(30), random_token());
            UserService(authorization)
                .provision(username, name, password, host_action == "--recover-admin", call);
            std::fill(password.begin(), password.end(), '\0');
            std::fill(confirm.begin(), confirm.end(), '\0');
            std::cout << "Administrator saved; password change required at next login\n";
            return 0;
        }
        LOG_INFO("ems-web starting");
        const auto result = ems::web::run_web(options);
        LOG_INFO("ems-web stopped, exit_code={}", result);
        return result;
    } catch (const ems::web::ApiError& error) {
        LOG_ERROR("ems-web failed: {} / {}", error.code, error.reason);
        std::cerr << "ems-web failed: " << error.code << " / " << error.reason << '\n';
        return 1;
    } catch (const std::exception&) {
        // 不输出配置内容、数据库 DSN 或带敏感信息的底层异常。
        LOG_ERROR("ems-web startup failed: check configuration, database and schema");
        std::cerr << "ems-web startup failed: check configuration, database and schema\n";
        return 1;
    }
}

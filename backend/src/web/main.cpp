#include <iostream>

#include "log/default_logger.hpp"
#include "log/process_logger.hpp"
#include "web/app/web_options.h"
#include "web/platform/crypto.h"
#include "web/web_log.h"

int main(int argc, char** argv) {
    const ProcessLogger logger("ems-web");
    ems::web::configure_framework_logging();
    try {
        std::string config = "conf/web.json";
        bool check = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--hash-password") {
                // 密码从标准输入读取，不将明文写入进程参数或日志。
                std::string password;
                std::getline(std::cin, password);
                std::cout << ems::web::hash_password(password) << '\n';
                return 0;
            }
            if (arg == "--check-config")
                check = true;
            else if (arg == "--config" && i + 1 < argc)
                config = argv[++i];
            else if (arg == "--help") {
                std::cout << "ems-web [--config path] [--check-config] | --hash-password\n";
                return 0;
            } else
                throw std::invalid_argument("unknown argument");
        }

        LOG_INFO("ems-web loading configuration: {}", config);
        const auto options = ems::web::load_web_options(config);
        if (check) {
            LOG_INFO("ems-web configuration valid");
            std::cout << "Web configuration valid\n";
            return 0;
        }
        LOG_INFO("ems-web starting");
        const auto result = ems::web::run_web(options);
        LOG_INFO("ems-web stopped, exit_code={}", result);
        return result;
    } catch (const std::exception&) {
        // 不输出配置内容、数据库 DSN 或带敏感信息的底层异常。
        LOG_ERROR(
            "ems-web startup failed: check configuration, identity file, database and schema");
        std::cerr
            << "ems-web startup failed: check configuration, identity file, database and schema\n";
        return 1;
    }
}

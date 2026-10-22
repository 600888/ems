#include <common/database/mysql_database.h>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    if (argc < 4 || argc > 5) {
        std::cerr
            << "Usage: ems_point_import <points.xlsx> <device_id> <channel_id> [append|replace]\n"
               "Database connection uses EMS_DATABASE_CONFIG / EMS_MYSQL_* environment "
               "variables.\n";
        return 2;
    }
    try {
        auto id = [](const std::string& text) {
            std::size_t consumed = 0;
            const auto value = std::stoll(text, &consumed);
            if (consumed != text.size() || value <= 0)
                throw std::invalid_argument("ID must be a positive integer");
            return value;
        };
        const auto device_id = id(argv[2]);
        const auto channel_id = id(argv[3]);
        const std::string mode = argc == 5 ? argv[4] : "append";
        if (mode != "append" && mode != "replace")
            throw std::invalid_argument("Mode must be append or replace");
        auto options = ems::database::load_mysql_options();
        if (!ems::core::is_ok(options)) {
            std::cerr << ems::core::error_of(options).message << '\n';
            return 2;
        }
        std::get<0>(options).require_configuration_schema = true;
        auto context = ems::database::connect_mysql_database(
            ems::core::value_of(options), ems::core::make_call_context(std::chrono::seconds(10)));
        if (!ems::core::is_ok(context)) {
            std::cerr << ems::core::error_of(context).message << '\n';
            return 1;
        }
        auto& database = ems::core::value_of(context);
        auto imported = ems::database::import_configuration_points(
            *database.database, *database.configuration, argv[1], channel_id, device_id,
            mode == "replace" ? ems::database::PointTableImportMode::Replace
                              : ems::database::PointTableImportMode::Append,
            ems::core::make_call_context(std::chrono::seconds(30)));
        if (!ems::core::is_ok(imported)) {
            const auto& error = ems::core::error_of(imported);
            std::cerr << ems::core::to_string(error.code) << ": " << error.message << '\n';
            if (error.effect == ems::core::WriteEffect::Unknown)
                std::cerr << "Commit result is unknown. Reconcile before retrying.\n";
            return 1;
        }
        const auto& result = ems::core::value_of(imported);
        if (!result.committed) {
            for (const auto& error : result.errors) {
                std::cerr << error.sheet << ':' << error.line << ':' << error.column << ": "
                          << error.reason << '\n';
            }
            return 1;
        }
        std::cout << "Committed " << result.inserted << " points (YC=" << result.data.yc.size()
                  << ", YX=" << result.data.yx.size() << ", YK=" << result.data.yk.size()
                  << ", YT=" << result.data.yt.size() << ")\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}

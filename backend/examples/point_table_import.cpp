#include <db/db_manager.h>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    if (argc < 4 || argc > 5) {
        std::cerr
            << "Usage: ems_point_import <points.xlsx> <device_id> <channel_id> [append|replace]\n"
               "Database connection uses EMS_DB_CONF / EMS_DB_* environment variables.\n";
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
        ems::db::DbManager database;
        if (!database.connect(ems::db::get_default_db_config())) {
            std::cerr << database.last_error() << '\n';
            return 1;
        }
        const auto result =
            database.import_point_table(argv[1], channel_id, device_id,
                                        mode == "replace" ? ems::db::PointTableImportMode::Replace
                                                          : ems::db::PointTableImportMode::Append);
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

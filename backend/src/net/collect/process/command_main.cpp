#include <rtdb/client/command_client.h>

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "ems-command <rtdb-config> <scope> <command_id> "
                     "<submit.json|--query|--verify> [notification-endpoint]\n";
        return 0;
    }
    if (argc != 5 && argc != 6) {
        std::cerr << "Use ems-command --help\n";
        return 2;
    }
    using namespace ems;
    const auto ctx = core::make_call_context(std::chrono::seconds(3));
    auto opened = rtdb::CommandClient::open(argv[1], {rtdb::ScopeKind::Station, argv[2]},
                                            argc == 6 ? argv[5] : "", ctx);
    if (!core::is_ok(opened)) {
        std::cerr << core::error_of(opened).message << '\n';
        return 1;
    }
    auto client = core::value_of(opened);
    const std::string id = argv[3], operation = argv[4];
    if (operation != "--query" && operation != "--verify") {
        try {
            std::ifstream input(operation, std::ios::binary);
            std::string bytes(2049, '\0');
            if (!input) throw std::runtime_error("Cannot open command JSON");
            input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            bytes.resize(static_cast<std::size_t>(input.gcount()));
            if (bytes.size() > 2048) throw std::runtime_error("Command JSON exceeds 2048 bytes");
            const auto payload = nlohmann::json::parse(bytes);
            if (!payload.is_object()) throw std::runtime_error("Command JSON must be an object");
            for (const auto& item : payload.items())
                if (item.key() != "op" && item.key() != "point_id" && item.key() != "value" &&
                    item.key() != "config_revision" && item.key() != "expires_at_ms")
                    throw std::runtime_error("Unknown command field");
            if (payload.value("op", std::string("submit")) != "submit" ||
                !payload.at("point_id").is_string() || !payload.at("value").is_number() ||
                !payload.at("config_revision").is_number_integer() ||
                !payload.at("expires_at_ms").is_number_integer())
                throw std::runtime_error("Invalid command field");
            auto result = client->submitCommand(
                {id, payload.at("point_id").get<std::string>(), payload.at("value").get<double>(),
                 payload.at("config_revision").get<std::int64_t>(),
                 payload.at("expires_at_ms").get<std::int64_t>()},
                ctx);
            if (!core::is_ok(result)) {
                std::cerr << core::error_of(result).message << '\n';
                return 1;
            }
            std::cout << (core::value_of(result).duplicate ? "Duplicate" : "Accepted") << '\n';
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 2;
        }
    }
    auto result = operation == "--verify" ? client->requestVerification(id, ctx)
                                          : client->readCommand(id, ctx);
    if (!core::is_ok(result)) {
        std::cerr << core::error_of(result).message << '\n';
        return 1;
    }
    const auto& record = core::value_of(result);
    std::cout << nlohmann::json{{"command_id", id},
                                {"point_id", record.request.point_id},
                                {"value", record.request.value},
                                {"status", record.status},
                                {"reason", record.reason},
                                {"sent_at_ms", record.sent_at_ms},
                                {"updated_at_ms", record.updated_at_ms}}
                     .dump()
              << '\n';
    return 0;
}

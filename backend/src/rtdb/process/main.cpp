#include <rtdb/client/realtime_client.h>

#include <csignal>
#include <iostream>
#include <thread>

#include "log/default_logger.hpp"
#include "log/process_logger.hpp"

namespace {
volatile std::sig_atomic_t stopping = 0;

void stop(int) { stopping = 1; }
}  // namespace

int main(int argc, char** argv) {
    const ProcessLogger logger("ems-rtdb");
    if (argc != 3 && argc != 4) {
        LOG_ERROR("ems-rtdb: expected --config <json-file> [--init-only]");
        std::cerr << "Usage: ems-rtdb --config <json-file> [--init-only]\n";
        return 2;
    }
    if (std::string(argv[1]) != "--config" ||
        (argc == 4 && std::string(argv[3]) != "--init-only")) {
        LOG_ERROR("ems-rtdb: invalid command line arguments");
        return 2;
    }
    LOG_INFO("ems-rtdb starting, config={}", argv[2]);
    auto result = ems::rtdb::RealtimeClient::initialize(
        argv[2], ems::core::make_call_context(std::chrono::seconds(5)));
    if (!ems::core::is_ok(result)) {
        const auto& error = ems::core::error_of(result);
        LOG_ERROR("ems-rtdb initialization failed: {}: {}", ems::core::to_string(error.code),
                  error.message);
        std::cerr << ems::core::to_string(error.code) << ": " << error.message << '\n';
        return 1;
    }
    auto client = std::get<0>(std::move(result));
    LOG_INFO("ems-rtdb ready: {} generation={}", client->info().implementation,
             client->info().storage_generation);
    std::cout << "ems-rtdb ready: " << client->info().implementation
              << " generation=" << client->info().storage_generation << std::endl;
    if (argc == 4) {
        client->close();
        LOG_INFO("ems-rtdb initialization complete, exiting");
        return 0;
    }
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    while (!stopping) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    LOG_INFO("ems-rtdb stopping");
    client->close();
    LOG_INFO("ems-rtdb stopped");
    return 0;
}

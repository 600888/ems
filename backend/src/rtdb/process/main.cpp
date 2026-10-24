#include <rtdb/client/realtime_client.h>

#include <csignal>
#include <iostream>
#include <thread>

namespace {
volatile std::sig_atomic_t stopping = 0;

void stop(int) { stopping = 1; }
}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::cerr << "Usage: ems-rtdb --config <json-file> [--init-only]\n";
        return 2;
    }
    if (std::string(argv[1]) != "--config" || (argc == 4 && std::string(argv[3]) != "--init-only"))
        return 2;
    auto result = ems::rtdb::RealtimeClient::initialize(
        argv[2], ems::core::make_call_context(std::chrono::seconds(5)));
    if (!ems::core::is_ok(result)) {
        const auto& error = ems::core::error_of(result);
        std::cerr << ems::core::to_string(error.code) << ": " << error.message << '\n';
        return 1;
    }
    auto client = std::get<0>(std::move(result));
    std::cout << "ems-rtdb ready: " << client->info().implementation
              << " generation=" << client->info().storage_generation << std::endl;
    if (argc == 4) return 0;
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    while (!stopping) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    client->close();
}

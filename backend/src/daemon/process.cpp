#include "process.h"

#include <ctime>

#include "spdlog/fmt/fmt.h"
#include "spdlog/fmt/ranges.h"

namespace ems::daemon {

void Process::record_exit(int exit_code, int term_signal) {
    this->exit_code = exit_code;
    this->term_signal = term_signal;
    pid = 0;
    process_handle = nullptr;
    status = ProcessStatus::Stopped;
}

std::string Process::description() const {
    std::string last_start_text;
    if (last_start_wall.time_since_epoch().count() != 0) {
        const std::time_t wall = std::chrono::system_clock::to_time_t(last_start_wall);
        char buffer[32] = {};
        std::tm local_time{};
        int converted = 0;
#ifdef _WIN32
        if (::localtime_s(&local_time, &wall) == 0) {
#else
        if (::localtime_r(&wall, &local_time) != nullptr) {
#endif
            converted = std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local_time);
        }
        if (converted > 0) {
            last_start_text = buffer;
        }
    }

    std::string text = fmt::format(
        "name={}, cmd={}, args=[{}], singleInstance={}, maxStartCount={}, delaySeconds={}s, "
        "startIntervalSeconds={}s, startCount={}, pid={}",
        name, cmd, fmt::join(args, " "), single_instance ? "true" : "false",
        restart_unlimited ? -1 : max_start_count, start_delay.count(), start_interval.count(),
        start_count, pid);
    if (!last_start_text.empty()) {
        text = fmt::format("{}, lastStartTime={}", text, last_start_text);
    }
    return text;
}

}  // namespace ems::daemon

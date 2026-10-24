#include "pid_file.h"

#include <filesystem>
#include <system_error>

namespace ems::daemon {

bool ensure_pid_directory(const std::string& dir, std::string& error) {
    if (dir.empty()) {
        error = "pid 目录为空";
        return false;
    }

    std::error_code code;
    if (std::filesystem::exists(dir, code)) {
        return true;
    }

    std::filesystem::create_directories(dir, code);
    if (code) {
        error = code.message();
        return false;
    }
    return true;
}

}  // namespace ems::daemon

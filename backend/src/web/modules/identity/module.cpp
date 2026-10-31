#include "module.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace ems::web {
const std::vector<Route>& identity_routes() {
    static const std::vector<Route> routes = {{"/api/auth/login-options", false},
                                              {"/api/auth/login", true},
                                              {"/api/auth/session", false},
                                              {"/api/auth/password/complete", true},
                                              {"/api/auth/reauthenticate", true},
                                              {"/api/auth/activity", true},
                                              {"/api/auth/logout", true},
                                              {"/api/auth/logout-all", true},
                                              {"/api/users/capabilities", false},
                                              {"/api/users/list", true},
                                              {"/api/users/detail", true},
                                              {"/api/users/create", true},
                                              {"/api/users/update", true},
                                              {"/api/users/set-status", true},
                                              {"/api/users/set-access", true},
                                              {"/api/users/reset-password", true},
                                              {"/api/users/security-settings", false},
                                              {"/api/users/security-settings/update", true},
                                              {"/api/users/unlock", true},
                                              {"/api/users/delete", true},
                                              {"/api/users/sessions/list", true},
                                              {"/api/users/sessions/revoke", true},
                                              {"/api/profile/me", false},
                                              {"/api/profile/update", true},
                                              {"/api/profile/change-password", true},
                                              {"/api/profile/sessions/list", true},
                                              {"/api/profile/sessions/revoke", true}};
    return routes;
}

std::string load_identity_key(const std::string& path) {
    auto file = path;
    if (file.empty()) {
        auto value = std::getenv("EMS_WEB_EDIT_KEY_FILE");
        if (value) file = value;
    }
    if (file.empty()) throw std::invalid_argument("identity key missing");
    auto native = std::filesystem::u8path(file);
    if (std::filesystem::file_size(native) > 4096) throw std::invalid_argument("identity key size");
    std::ifstream input(native, std::ios::binary);
    std::string key((std::istreambuf_iterator<char>(input)), {});
    if (key.size() < 32 || !input) throw std::invalid_argument("identity key size");
    return key;
}
}  // namespace ems::web

#include "module_registry.h"

namespace ems::web {
const std::vector<Route>& device_routes() {
    static const std::vector<Route> routes = {
        {"/api/devices/tree", true},
        {"/api/devices/list", true},
        {"/api/devices/detail", true},
        {"/api/devices/create", true},
        {"/api/devices/update", true},
        {"/api/devices/delete", true},
        {"/api/device-types/list", true},
        {"/api/device-types/points", true},
        {"/api/device-templates/list", true},
        {"/api/device-templates/detail", true},
        {"/api/device-templates/apply", true},
        {"/api/device-management/capabilities", false},
        {"/api/channels/list", true},
        {"/api/channels/detail", true},
        {"/api/channels/create", true},
        {"/api/channels/update", true},
        {"/api/channels/delete", true},
        {"/api/bindings/list", true},
        {"/api/bindings/create", true},
        {"/api/bindings/update", true},
        {"/api/bindings/delete", true},
        {"/api/inherent-points/list", true},
        {"/api/points/list", true},
        {"/api/points/detail", true},
        {"/api/points/create", true},
        {"/api/points/update", true},
        {"/api/points/delete", true},
        {"/api/points/batch-change", true},
        {"/api/point-imports/create", true},
        {"/api/point-imports/list", true},
        {"/api/point-imports/detail", true},
        {"/api/point-imports/rows", true},
        {"/api/point-imports/update-row", true},
        {"/api/point-imports/validate", true},
        {"/api/point-imports/apply", true},
        {"/api/device-management/configuration", false},
        {"/api/device-management/validate", true},
    };
    return routes;
}
}  // namespace ems::web

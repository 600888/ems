/**
 * @file realtime_store.cpp
 * @brief RecordKey 公共验证器实现（access-design.md 补充契约 5.1-2）
 */
#include <realtime/realtime_store.h>

#include "store_detail.h"

namespace ems::realtime {

std::string validate_record_key(const RecordKey& key) {
    if (key.scope.kind != ScopeKind::Station && key.scope.kind != ScopeKind::Unit)
        return "invalid scope kind";
    if (!detail::valid_code(key.scope.code)) return "invalid scope code";
    if (key.device_path.size() > 8) return "device path depth exceeded";
    for (const auto& part : key.device_path)
        if (!detail::valid_code(part, true)) return "invalid/reserved device path code";
    if (key.kind != RecordKind::AssetState && key.kind != RecordKind::CommandState &&
        !detail::valid_code(key.name))
        return "invalid point/channel code";
    if (key.kind == RecordKind::CommandState) {
        if (key.name.empty() || key.name.size() > 128) return "invalid command ID";
        for (auto c : key.name)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-'))
                return "invalid command ID";
    }
    switch (key.kind) {
        case RecordKind::ChannelState:
            // ChannelState 只允许 station 作用域且设备路径为空
            if (key.scope.kind != ScopeKind::Station) {
                return "ChannelState 只允许 station 作用域";
            }
            if (!key.device_path.empty()) {
                return "ChannelState 设备路径必须为空";
            }
            if (key.name.empty()) {
                return "ChannelState 缺少通道 code";
            }
            break;
        case RecordKind::CommandState:
            // CommandState 的 name 为命令 ID 且路径为空
            if (!key.device_path.empty()) {
                return "CommandState 设备路径必须为空";
            }
            if (key.name.empty()) {
                return "CommandState 缺少命令 ID";
            }
            break;
        case RecordKind::Point:
            // Point 允许根级测点（设备路径可为空）
            if (key.name.empty()) {
                return "Point 缺少 point code";
            }
            if (key.device_path.size() > 8) {
                return "Point 设备路径超过最大深度";
            }
            break;
        case RecordKind::AssetState:
            // AssetState 的 name 为空
            if (!key.name.empty()) {
                return "AssetState 不允许 name 字段";
            }
            break;
        default:
            return "invalid record kind";
    }
    if (key.scope.code.empty()) {
        return "scope code 为空";
    }
    return {};
}

}  // namespace ems::realtime

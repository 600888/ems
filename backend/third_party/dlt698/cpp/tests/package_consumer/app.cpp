/** @file app.cpp
 * @brief 只通过安装包统一库目标验证高层入口与共享库导出。
 */
#include <dlt698/app.hpp>

int main() {
    dlt698::app::ServerOptions server_options;
    server_options.traffic = [](std::uint64_t, const dlt698::session::TrafficEvent&) {};
    dlt698::app::ClientOptions client_options;
    client_options.traffic = [](const dlt698::session::TrafficEvent&) {};
    dlt698::app::Server server(server_options);
    dlt698::app::Client client(client_options);
    if (client.connect_tcp("", 6980) || !client.disconnect()) return 5;
    if (client.get({0x200F, 2, 0}).error().code != dlt698::ErrorCode::not_associated) return 6;
    if (!server.set({0x200F, 2, 0}, dlt698::model::UInt16{5000})) return 1;
    // 不依赖回环网络权限；安装消费方验证启动失败回收及显式停止的公开符号。
    if (server.start_tcp("invalid-ip", 0)) return 2;
    if (!server.stop() || server.state() != dlt698::app::ServerState::stopped) return 3;
    return std::get<dlt698::model::Data>(server.device()->get({0x200F, 2, 0}))
                       .as<dlt698::model::UInt16>()
                       .value == 5000
               ? 0
               : 4;
}

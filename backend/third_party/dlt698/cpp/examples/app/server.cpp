/** @file server.cpp
 * @brief 托管服务端：发布模拟三相电表数据，启动 TCP 或串口，然后等待应用退出。
 */
#include <dlt698/app.hpp>
#include <iostream>
#include <utility>
#include <vector>

int main(int argc, char** argv) {
    using namespace dlt698;
    namespace oi = standard::oi;
    app::ServerOptions options;
    // 电表地址（SA）默认 000000000000，用六个字节表示；客户端必须配置相同地址。
    // 按线序填写，低有效字节在前；例如 123456789012 对应 12 90 78 56 34 12。
    options.protocol.server.bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    options.protocol.server.logical = 0;  // 逻辑地址，范围 0～3。
    options.protocol.client_address = 0;  // 客户端地址（CA），两端必须一致。
    app::Server server(options);
    // 默认设备为三相、四费率；这些对象的属性 2 为数据属性，索引 0 发布完整属性。
    // 以下均为固定模拟值，按协议倍率保存整数；数组长度须与设备布局一致。
    const std::vector<std::pair<model::Oad, model::Data>> simulated_values{
        // 200F/2/0：电网频率，UInt16，单位 0.01 Hz；5000 表示 50.00 Hz。
        {{oi::frequency, 2, 0}, model::UInt16{5000}},
        // 2000/2/0：A/B/C 相电压，UInt16 数组，单位 0.1 V；230.0/231.0/229.0 V。
        {{oi::voltage, 2, 0},
         model::Array{{model::UInt16{2300}, model::UInt16{2310}, model::UInt16{2290}}}},
        // 2001/2/0：A/B/C 相电流，Int32 数组，单位 0.001 A；5.000/10.000/15.000 A。
        {{oi::current, 2, 0},
         model::Array{{model::Int32{5000}, model::Int32{10000}, model::Int32{15000}}}},
        // 2004/2/0：总/A/B/C 有功功率，Int32 数组，单位 0.1 W；6000/1000/2000/3000 W。
        {{oi::active_power, 2, 0},
         model::Array{
             {model::Int32{60000}, model::Int32{10000}, model::Int32{20000}, model::Int32{30000}}}},
        // 2005/2/0：总/A/B/C 无功功率，Int32 数组，单位 0.1 var；3000/500/1000/1500 var。
        {{oi::reactive_power, 2, 0},
         model::Array{
             {model::Int32{30000}, model::Int32{5000}, model::Int32{10000}, model::Int32{15000}}}},
        // 200A/2/0：总/A/B/C 功率因数，Int16 数组，倍率 0.001，无单位；均为 0.894。
        {{oi::power_factor, 2, 0},
         model::Array{
             {model::Int16{894}, model::Int16{894}, model::Int16{894}, model::Int16{894}}}},
        // 0010/2/0：正向有功电能，UInt32 数组，单位 0.01 kWh，顺序为总量、费率 1～4。
        // 总量 1234.56 kWh；四费率分别为 100.00、200.00、300.00、634.56 kWh。
        {{oi::forward_active_energy, 2, 0},
         model::Array{{model::UInt32{123456}, model::UInt32{10000}, model::UInt32{20000},
                       model::UInt32{30000}, model::UInt32{63456}}}},
        // 4001/2/0：电能表通信地址，OctetString；复用上方 SA 配置，默认六个 00 字节。
        // 此处仅发布可读取的属性；实际报文寻址由创建服务器时传入的 options 决定。
        {{oi::communication_address, 2, 0}, model::OctetString{options.protocol.server.bytes}},
    };
    // 所有属性校验成功后才启动监听，避免客户端读到只发布了一部分的模拟数据。
    for (const auto& entry : simulated_values) {
        auto published = server.set(entry.first, entry.second);
        if (!published) {
            std::cerr << "Publish OI=" << entry.first.oi << ": " << published.error().context
                      << '\n';
            return 1;
        }
    }
    // 无参数监听 TCP；提供串口名时使用默认 9600/8E1，库内部驱动事件循环。
    auto started = argc == 1 ? server.start_tcp("0.0.0.0", 6980) : server.start_serial(argv[1]);
    if (!started) {
        std::cerr << started.error().context << '\n';
        return 1;
    }
    std::cout << "Serving " << simulated_values.size()
              << " simulated meter attributes. Press Enter to stop.\n";
    std::cin.get();
    const auto stopped = server.stop();
    if (!stopped) {
        std::cerr << stopped.error().context << '\n';
        return 1;
    }
    return 0;
}

/** @file client.cpp
 * @brief 与托管服务器配对的客户机：连接、读取模拟三相电表数据、断开。
 */
#include <dlt698/app.hpp>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    using namespace dlt698;
    namespace oi = standard::oi;
    app::ClientOptions options;
    // 要访问的电表地址（SA）默认 000000000000，用六个字节表示；必须与服务端一致。
    // 按线序填写，低有效字节在前；例如 123456789012 对应 12 90 78 56 34 12。
    options.protocol.server.bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    options.protocol.server.logical = 0;  // 逻辑地址，范围 0～3。
    options.protocol.client_address = 0;  // 客户端地址（CA），两端必须一致。
    app::Client client(options);
    // 无参数连接本机服务器；提供串口名时自动使用默认 9600/8E1 和本地 CONNECT。
    auto connected =
        argc == 1 ? client.connect_tcp("127.0.0.1", 6980) : client.open_serial(argv[1]);
    if (!connected) {
        std::cerr << connected.error().context << '\n';
        return 1;
    }
    // OAD = 对象标识 / 属性编号 / 元素索引；索引 0 读取完整属性，非零从 1 开始。
    const std::vector<model::Oad> attributes{
        {oi::frequency, 2, 0},       // 200F：电网频率，预期 50.00 Hz。
        {oi::voltage, 2, 0},         // 2000：A/B/C 相电压，230.0/231.0/229.0 V。
        {oi::current, 2, 0},         // 2001：A/B/C 相电流，5.000/10.000/15.000 A。
        {oi::active_power, 2, 0},    // 2004：总/A/B/C 有功功率，6000/1000/2000/3000 W。
        {oi::reactive_power, 2, 0},  // 2005：总/A/B/C 无功功率，3000/500/1000/1500 var。
        {oi::power_factor, 2, 0},    // 200A：总/A/B/C 功率因数，均为 0.894，无单位。
        // 0010：总/费率 1～4 电能，1234.56/100/200/300/634.56 kWh。
        {oi::forward_active_energy, 2, 0},
        {oi::communication_address, 2, 0},  // 4001：通信地址，默认原始字节 00 00 00 00 00 00。
        {oi::voltage, 2, 1},                // 2000/2/1：仅 A 相电压，230.0 V。
        {oi::forward_active_energy, 2, 2},  // 0010/2/2：仅费率 1 电能，100.00 kWh；索引 1 为总量。
    };
    auto result = client.get_list(attributes);
    if (!result) {
        std::cerr << result.error().context << '\n';
        return 1;
    }
    bool read_failed = false;
    for (const auto& entry : result.value().attributes) {
        const auto* object = standard::find_object(entry.attribute.oi);
        std::cout << (object ? object->name : "Unknown object")
                  << " [index=" << unsigned(entry.attribute.index) << "]=";
        // 列表允许部分成功，逐项检查 DAR；单项失败仍展示其它项，最终返回非零退出码。
        if (const auto dar = std::get_if<std::uint8_t>(&entry.result)) {
            std::cout << "DAR=" << unsigned(*dar) << '\n';
            read_failed = true;
            continue;
        }
        const auto& data = std::get<model::Data>(entry.result);
        if (entry.attribute.oi == oi::communication_address) {
            // 通信地址是字节串，不是带倍率的数值；先校验类型，再展示原始十六进制字节。
            auto valid = standard::validate_value(entry.attribute, data);
            if (!valid) {
                std::cout << valid.error().context << '\n';
                read_failed = true;
                continue;
            }
            std::cout << to_hex(data.as<model::OctetString>().value);
        } else {
            // 依据标准目录校验类型/数组长度并换算倍率，避免直接把协议整数当作实际值。
            auto numbers = standard::engineering_values(entry.attribute, data);
            if (!numbers) {
                std::cout << numbers.error().context << '\n';
                read_failed = true;
                continue;
            }
            for (std::size_t i = 0; i < numbers.value().size(); ++i) {
                if (i != 0) std::cout << ", ";
                const auto& number = numbers.value()[i];
                std::cout << standard::decimal_text(number);
                const auto unit = standard::unit_symbol(number.scaling.unit);
                if (!unit.empty()) std::cout << ' ' << unit;
            }
        }
        std::cout << '\n';
    }
    const auto disconnected = client.disconnect();
    if (!disconnected) {
        std::cerr << disconnected.error().context << '\n';
        return 1;
    }
    return read_failed ? 1 : 0;
}

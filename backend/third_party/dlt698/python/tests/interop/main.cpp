#include <dlt698/app.hpp>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    const dlt698::model::Oad frequency{0x200f, 2, 0};
    // 独立安装包消费方通过真实 TCP 交互，不能链接 Python 扩展作为测试替身。
    if (std::string(argv[1]) == "server") {
        dlt698::app::Server server;
        if (!server.set(frequency, dlt698::model::UInt16{5001}) ||
            !server.start_tcp("127.0.0.1", 0))
            return 2;
        std::cout << server.local_port() << std::endl;
        std::string stop;
        std::getline(std::cin, stop);
        return server.stop() ? 0 : 3;
    }
    if (argc != 3) return 4;
    dlt698::app::Client client;
    if (!client.connect_tcp("127.0.0.1", static_cast<std::uint16_t>(std::stoul(argv[2])))) return 5;
    auto result = client.get(frequency);
    if (!result || !std::holds_alternative<dlt698::model::Data>(result.value())) return 6;
    const auto& data = std::get<dlt698::model::Data>(result.value());
    if (data.type() != dlt698::model::DataType::uint16) return 7;
    std::cout << data.as<dlt698::model::UInt16>().value << std::endl;
    return client.disconnect() ? 0 : 8;
}

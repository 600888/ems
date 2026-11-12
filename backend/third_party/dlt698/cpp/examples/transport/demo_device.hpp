/**
 * @file demo_device.hpp
 * @brief TCP/RTU 对端示例共用的模拟设备目录、参数解析和命令执行。
 * @note 仅供示例使用，不属于库的公开接口。
 */
#pragma once
#include <csignal>
#include <dlt698/model/record.hpp>
#include <dlt698/protocol/apdu/get.hpp>
#include <dlt698/service/service.hpp>
#include <dlt698/service/sync.hpp>
#include <iostream>
#include <string>

namespace demo {

/** @brief SIGINT 标志，示例循环据此提前退出。 */
inline volatile std::sig_atomic_t interrupted = 0;

inline void on_interrupt(int) { interrupted = 1; }

/** @brief 安装 SIGINT 处理，允许示例走正常的关闭流程而不是被强杀。 */
inline void install_signal_handler() { std::signal(SIGINT, on_interrupt); }

/**
 * @brief 严格解析十进制无符号参数。
 * @param[in] text 命令行文本，非空且不含负号或尾随字符。
 * @param[in] maximum 允许的上界。
 * @param[in] allow_zero 是否允许零。
 * @return 解析出的数值。
 * @throws std::invalid_argument 文本格式非法或数值越界。
 */
inline unsigned number(const char* text, unsigned maximum, bool allow_zero = false) {
    const std::string input(text);
    std::size_t end = 0;
    const auto value = std::stoul(input, &end);
    if (end != input.size() || input.empty() || input.front() == '-' || (!value && !allow_zero) ||
        value > maximum)
        throw std::invalid_argument("numeric argument out of range: " + input);
    return static_cast<unsigned>(value);
}

/** @brief 打印精确 Data 的十六进制字节。
 * @param[in] data 协议原始值，不做单位或倍率换算。 */
inline void print_data(const dlt698::model::Data& data) {
    auto bytes = dlt698::codec::encode_data(data);
    if (!bytes) throw std::runtime_error(bytes.error().context);
    std::cout << "Data: " << dlt698::to_hex(bytes.value()) << '\n';
}

/** @brief 打印读取结果：DAR 直接给码值，否则给精确 Data 字节。 */
inline void print_value(const dlt698::service::ObjectValue& value) {
    if (const auto dar = std::get_if<std::uint8_t>(&value))
        std::cout << "DAR=" << unsigned(*dar) << '\n';
    else
        print_data(std::get<dlt698::model::Data>(value));
}

/**
 * @brief 构造示例设备目录，全部数据为固定模拟值，不代表任何真实计量能力。
 * @return 含 OI=0x2000 一个对象的目录：属性 2 可读可写 UInt16，方法 1 用 UInt16
 * 参数替换属性 2 并原样返回，属性 3 是只读的一行冻结记录。
 * @throws std::runtime_error 对象注册失败。
 */
inline std::shared_ptr<dlt698::service::ObjectRegistry> demo_registry() {
    using namespace dlt698;
    using namespace dlt698::model;
    using namespace dlt698::protocol;
    using namespace dlt698::service;
    auto registry = std::make_shared<ObjectRegistry>();
    auto object = std::make_shared<MemoryObject>();
    object->set(2, UInt16{42});
    // 方法回调只捕获 weak_ptr：目录持有 provider，若捕获 shared_ptr 会形成引用环。
    object->bind_method(1, [weak = std::weak_ptr<MemoryObject>(object)](const Omd&, const Data& p) {
        const auto self = weak.lock();
        if (!self) return ActionValue{2, {}};
        const auto value = p.as<UInt16>().value;
        self->set(2, UInt16{value});
        return ActionValue{0, Data{UInt16{value}}};
    });
    object->bind_record(3, [](const apdu::GetRecord& query) {
        // 示例只支持全选；采集时段、表计和存储器的筛选语义由应用后端实现。
        if (!std::holds_alternative<SelectAll>(query.rows))
            return apdu::RecordResult{query.attribute, query.columns, std::uint8_t{3}};
        Rcsd columns{Oad{0x2021, 2, 0}, Oad{0x2000, 2, 0}};
        if (!query.columns.empty() && !(query.columns == columns))
            return apdu::RecordResult{query.attribute, query.columns, std::uint8_t{3}};
        return apdu::RecordResult{
            query.attribute, std::move(columns),
            std::vector<apdu::RecordRow>{{DateTimeS{{7, 0xea, 10, 4, 0, 0, 0}}, UInt16{42}}}};
    });
    auto registered = registry->register_object(
        {0x2000,
         "模拟数据",
         {{2, DataType::uint16, true, true}, {3, DataType::null, true, false, true}},
         {{1, DataType::uint16, DataType::uint16, true}}},
        object);
    if (!registered) throw std::runtime_error(registered.error().context);
    return registry;
}

/**
 * @brief 在同步客户机上执行一次命令并打印结果。
 * @param[in] client 已完成 CONNECT 的同步客户机。
 * @param[in] command get、set、action 或 record。
 * @param[in] argument set/action 的无符号参数，其它命令忽略。
 * @throws std::runtime_error 本地错误或业务被拒绝。
 * @note 远端拒绝不能只看外层错误：GET 逐项 DAR、SET 返回的 DAR 都可能是非零成功码。
 */
inline void run_command(dlt698::service::SyncClientService& client, const std::string& command,
                        unsigned argument) {
    using namespace dlt698;
    using namespace dlt698::model;
    using namespace dlt698::protocol;
    if (command == "get") {
        auto r = client.get({0x2000, 2, 0});
        if (!r) throw std::runtime_error(r.error().context);
        print_value(r.value());
    } else if (command == "set") {
        auto r = client.set({0x2000, 2, 0}, UInt16{static_cast<std::uint16_t>(argument)});
        if (!r) throw std::runtime_error(r.error().context);
        std::cout << "DAR=" << unsigned(r.value()) << '\n';
    } else if (command == "action") {
        auto r = client.action({0x2000, 1, 0}, UInt16{static_cast<std::uint16_t>(argument)});
        if (!r) throw std::runtime_error(r.error().context);
        std::cout << "DAR=" << unsigned(r.value().dar) << '\n';
        if (r.value().data) print_data(*r.value().data);
    } else {
        auto r = client.get_record({{0x2000, 3, 0}, SelectAll{}, {}});
        if (!r) throw std::runtime_error(r.error().context);
        if (const auto dar = std::get_if<std::uint8_t>(&r.value().result))
            std::cout << "DAR=" << unsigned(*dar) << '\n';
        else
            for (const auto& row : std::get<std::vector<apdu::RecordRow>>(r.value().result))
                for (const auto& d : row) print_data(d);
    }
}
}  // namespace demo
/** @file device_test.cpp
 * @brief 数据发布、预算、失败原子性及并发快照验证。
 */
#include <atomic>
#include <dlt698/service/device.hpp>
#include <thread>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::model;
using namespace dlt698::service;

TEST_CASE("Device 只发布成功设置的属性并原子扩充同一 OI", "[service][device]") {
    Device device;
    CHECK(std::get<std::uint8_t>(device.get({0x2000, 2, 0})) == 4);
    REQUIRE(device.set({0x2000, 2, 0}, Array{{UInt16{2410}, UInt16{2411}, UInt16{2412}}}));
    const auto before = std::get<Data>(device.get({0x2000, 2, 0}));
    REQUIRE(device.set({0x2000, 3, 0}, ScalerUnit{-1, 38}));
    CHECK(std::get<Data>(device.get({0x2000, 2, 0})) == before);
    CHECK(std::get<Data>(device.get({0x2000, 3, 0})).as<ScalerUnit>() == ScalerUnit{-1, 38});
    REQUIRE(device.set_element({0x2000, 2, 2}, UInt16{2400}));
    CHECK(std::get<Data>(device.get({0x2000, 2, 2})).as<UInt16>().value == 2400);
    CHECK(before.as<Array>().value[1].as<UInt16>().value == 2411);
    CHECK(std::get<std::uint8_t>(device.get({0x2000, 0x22, 0})) == 3);
    CHECK(std::get<std::uint8_t>(device.get({0x2000, 2, 4})) == 8);
}

TEST_CASE("Device 拒绝错误结构和索引且保留旧值", "[service][device]") {
    Device device;
    REQUIRE(device.set({0x200F, 2, 0}, UInt16{5000}));
    CHECK_FALSE(device.set({0x200F, 2, 0}, UInt32{5000}));
    CHECK_FALSE(device.set({0x200F, 0x22, 0}, UInt16{4999}));
    CHECK_FALSE(device.set({0x200F, 2, 1}, UInt16{4999}));
    CHECK_FALSE(device.set_element({0x200F, 2, 1}, UInt16{4999}));
    CHECK_FALSE(device.set({0x2000, 2, 0}, UInt16{2400}));
    CHECK_FALSE(device.set({0x2000, 2, 0}, Array{{UInt16{2400}}}));
    CHECK_FALSE(device.set({0x2000, 2, 0}, Array{{UInt32{1}, UInt32{2}, UInt32{3}}}));
    CHECK(std::get<Data>(device.get({0x200F, 2, 0})).as<UInt16>().value == 5000);
    CHECK(std::get<std::uint8_t>(device.get({0x2000, 2, 0})) == 4);
}

TEST_CASE("Device 自定义定义不覆盖标准且声明不生成数据", "[service][device]") {
    Device device;
    CHECK_FALSE(device.set({0xf900, 2, 0}, UInt16{1}));
    CHECK_FALSE(device.define({0x200F, "标准冲突", {{2, DataType::uint16}}}));
    REQUIRE(device.define({0xf900, "自定义", {{2, DataType::uint16}, {3, DataType::array}}}));
    CHECK_FALSE(device.define({0xf900, "重复", {{2, DataType::uint16}}}));
    CHECK(std::get<std::uint8_t>(device.get({0xf900, 2, 0})) == 4);
    REQUIRE(device.set({0xf900, 2, 0}, UInt16{10}));
    REQUIRE(device.set({0xf900, 3, 0}, Array{{UInt16{20}, UInt16{30}}}));
    CHECK_FALSE(device.set({0xf900, 4, 0}, UInt16{40}));
    CHECK_FALSE(device.set({0xf900, 2, 0}, UInt32{40}));
    CHECK_FALSE(device.define({0xf901, "远程可写", {{2, DataType::uint16, true, true}}}));
    CHECK(std::get<Data>(device.get({0xf900, 3, 2})).as<UInt16>().value == 30);
}

TEST_CASE("Device 替换按净字节数计算且失败不占用名额", "[service][device]") {
    DeviceOptions options;
    options.max_objects = 2;
    options.max_attributes = 2;
    options.max_value_bytes = 6;
    Device device(options);
    REQUIRE(device.define({0xf900, "自定义", {{2, DataType::uint16}}}));
    REQUIRE(device.set({0xf900, 2, 0}, UInt16{1}));
    // 已声明且已发布的自定义 OI 只占一个对象名额。
    REQUIRE(device.set({0x200F, 2, 0}, UInt16{5000}));
    REQUIRE(device.set({0x200F, 2, 0}, UInt16{4999}));
    CHECK_FALSE(device.set({0x200F, 3, 0}, ScalerUnit{-2, 44}));
    CHECK_FALSE(device.define({0xf901, "超限", {{2, DataType::uint16}}}));
    CHECK(std::get<Data>(device.get({0x200F, 2, 0})).as<UInt16>().value == 4999);

    options.max_objects = 1;
    options.max_attributes = 2;
    options.max_value_bytes = 8;
    Device strings(options);
    REQUIRE(strings.define({0xf900, "字节预算", {{2, DataType::octet_string}}}));
    REQUIRE(strings.set({0xf900, 2, 0}, OctetString{{1, 2}}));
    CHECK_FALSE(strings.set({0xf900, 2, 0}, OctetString{Bytes(20, 0)}));
    REQUIRE(strings.set({0xf900, 2, 0}, OctetString{{3, 4, 5}}));
    CHECK(std::get<Data>(strings.get({0xf900, 2, 0})).as<OctetString>().value == Bytes{3, 4, 5});
}

TEST_CASE("Device 多线程首次发布和完整快照不会丢失属性或撕裂数组", "[service][device]") {
    Device device;
    std::atomic<bool> success{true};
    std::thread values([&] {
        for (std::uint16_t i = 1; i < 300; ++i)
            if (!device.set({0x2000, 2, 0}, Array{{UInt16{i}, UInt16{i}, UInt16{i}}}))
                success = false;
    });
    std::thread units([&] {
        for (int i = 0; i < 100; ++i)
            if (!device.set({0x2000, 3, 0}, ScalerUnit{-1, 38})) success = false;
    });
    std::thread reader([&] {
        for (int i = 0; i < 500; ++i) {
            const auto result = device.get({0x2000, 2, 0});
            if (const auto data = std::get_if<Data>(&result)) {
                const auto& entries = data->as<Array>().value;
                if (entries.size() != 3 || !(entries[0] == entries[1]) ||
                    !(entries[0] == entries[2]))
                    success = false;
            }
        }
    });
    values.join();
    units.join();
    reader.join();
    CHECK(success.load());
    CHECK(std::get<Data>(device.get({0x2000, 2, 1})).as<UInt16>().value == 299);
    CHECK(std::holds_alternative<Data>(device.get({0x2000, 3, 0})));
}

TEST_CASE("Device 配置必须有效", "[service][device]") {
    DeviceOptions options;
    options.max_objects = 0;
    CHECK_THROWS_AS(Device(options), std::invalid_argument);
    options.max_objects = 1;
    options.layout.tariff_count = 255;
    CHECK_THROWS_AS(Device(options), std::invalid_argument);
}

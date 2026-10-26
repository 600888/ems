#include <gtest/gtest.h>

#include <cstdlib>
#include <nlohmann/json.hpp>

#include "net/para_loader/cfg_loader.h"

namespace {
using namespace ems::database;
using ems::net::device::CfgLoader;

DeviceConfigRecords configuration() {
    DeviceConfigRecords result;
    CfgDeviceRow station;
    station.id = 1;
    station.device_id = "station";
    station.node_kind = "station";
    station.config_revision = 7;
    result.devices.push_back(station);
    CfgDeviceRow source;
    source.id = 2;
    source.device_id = "source";
    source.parent_device_id = "station";
    source.node_kind = "device";
    source.name = "BMS";
    result.devices.push_back(source);
    CfgDeviceRow component;
    component.id = 3;
    component.device_id = "rack";
    component.parent_device_id = "source";
    component.node_kind = "component";
    component.source_device_id = "source";
    component.name = "Rack";
    result.devices.push_back(component);
    CfgChannelRow channel;
    channel.id = 7;
    channel.channel_id = "channel";
    channel.protocol = "modbus";
    channel.conn_mode = "tcp_client";
    channel.endpoint = "127.0.0.1:1502";
    result.channels.push_back(channel);
    CfgBindingRow binding;
    binding.id = 10;
    binding.binding_id = "binding";
    binding.device_id = "source";
    binding.channel_id = "channel";
    binding.device_address = "42";
    binding.can_collect = 1;
    binding.can_control = 1;
    result.bindings.push_back(binding);
    CfgPointYcRow yc;
    yc.id = 1;
    yc.point_id = "point-rack-voltage";
    yc.owner_device_id = "rack";
    yc.binding_id = "binding";
    yc.code = "voltage";
    yc.address_space = "holding_register";
    yc.address_offset = 12;
    yc.wire_type = "f32";
    yc.register_count = 2;
    yc.byte_order = "ba";
    yc.scale_factor = 0.1;
    yc.offset_value = -2;
    result.points_yc.push_back(yc);
    return result;
}

TEST(CfgLoader, ReadsNewBindingsAndPreservesComponentIdentityAndStablePointId) {
    CfgLoader loader;
    ASSERT_TRUE(loader.configure(configuration())) << loader.last_error();
    ASSERT_EQ(loader.channels().size(), 1u);
    EXPECT_EQ(loader.channels()[0].protocol_, ems::model::ProtocolType::ModbusTCP);
    const auto devices = loader.devices(7);
    ASSERT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0].deviceKey_, "rack");
    EXPECT_EQ(devices[0].deviceId_, 3);
    ASSERT_EQ(devices[0].ycPoints_.size(), 1u);
    EXPECT_EQ(devices[0].ycPoints_[0].point_id, "point-rack-voltage");
    EXPECT_EQ(devices[0].ycPoints_[0].protocol_code, "42:3:12:4:2");
    EXPECT_DOUBLE_EQ(devices[0].ycPoints_[0].scale_factor, 0.1);
    EXPECT_DOUBLE_EQ(devices[0].ycPoints_[0].offset_value, -2);
    EXPECT_EQ(loader.config_revision(), 7);
}

TEST(CfgLoader, SupportsSharedChannelsWithPerBindingAddresses) {
    auto records = configuration();
    auto node = records.devices[1];
    node.id = 4;
    node.device_id = "second";
    records.devices.push_back(node);
    auto binding = records.bindings[0];
    binding.binding_id = "second-binding";
    binding.device_id = "second";
    binding.device_address = "43";
    records.bindings.push_back(binding);
    auto point = records.points_yc[0];
    point.point_id = "second-point";
    point.owner_device_id = "second";
    point.binding_id = "second-binding";
    records.points_yc.push_back(point);
    CfgLoader loader;
    ASSERT_TRUE(loader.configure(records)) << loader.last_error();
    EXPECT_EQ(loader.channels().size(), 1u);
    ASSERT_EQ(loader.devices(7).size(), 2u);
    EXPECT_EQ(loader.devices(7)[1].ycPoints_[0].protocol_code, "43:3:12:4:2");
}

TEST(CfgLoader, DisablingSourceOrBindingOrChannelStopsTheirCollection) {
    auto records = configuration();
    CfgLoader loader;
    records.devices[1].enabled = 0;
    ASSERT_TRUE(loader.configure(records));
    EXPECT_TRUE(loader.channels().empty());
    records = configuration();
    records.bindings[0].enabled = 0;
    ASSERT_TRUE(loader.configure(records));
    EXPECT_TRUE(loader.devices(7).empty());
    records = configuration();
    records.channels[0].enabled = 0;
    ASSERT_TRUE(loader.configure(records));
    EXPECT_TRUE(loader.channels().empty());
}

TEST(CfgLoader, FailedConfigurationRetainsPreviousValidRoutes) {
    CfgLoader loader;
    ASSERT_TRUE(loader.configure(configuration()));
    auto records = configuration();
    records.points_yc[0].binding_id = "missing";
    EXPECT_FALSE(loader.configure(records));
    EXPECT_FALSE(loader.last_error().empty());
    EXPECT_EQ(loader.devices(7)[0].ycPoints_[0].point_id, "point-rack-voltage");
    records = configuration();
    records.bindings[0].can_collect = 0;
    EXPECT_FALSE(loader.configure(records));
    records = configuration();
    records.points_yc[0].register_count = 1;
    EXPECT_FALSE(loader.configure(records));
}

TEST(CfgLoader, SerialFieldsAndTimeoutUseNewChannelParameters) {
    auto records = configuration();
    records.channels[0].conn_mode = "serial";
    records.channels[0].endpoint = "COM3";
    records.channels[0].serial_json =
        R"({"baud_rate":19200,"data_bits":8,"stop_bits":2,"parity":"E"})";
    records.channels[0].timeout_ms = 250;
    CfgLoader loader;
    ASSERT_TRUE(loader.configure(records)) << loader.last_error();
    const auto channel = loader.channels()[0];
    EXPECT_EQ(channel.protocol_, ems::model::ProtocolType::ModbusRTU);
    EXPECT_EQ(channel.baudRate_, 19200);
    EXPECT_EQ(channel.stopBits_, 2);
    EXPECT_EQ(channel.parity_, "E");
    EXPECT_EQ(nlohmann::json::parse(channel.settings_)["timeout_ms"], 250);
}

TEST(CfgLoader, ReadsIec104AndDlt645ProtocolAddressJson) {
    auto records = configuration();
    records.channels[0].protocol = "iec104";
    records.points_yc[0].protocol_address_json =
        R"({"casdu":2,"ioa":100,"iec104_type":"M_ME_NC_1"})";
    CfgLoader loader;
    ASSERT_TRUE(loader.configure(records)) << loader.last_error();
    EXPECT_EQ(loader.devices(7)[0].ycPoints_[0].protocol_code, "2:100:13");
    records.channels[0].protocol = "dlt645";
    records.bindings[0].device_address = "123456789012";
    records.points_yc[0].protocol_address_json = R"({"data_id":"0x02010100"})";
    ASSERT_TRUE(loader.configure(records)) << loader.last_error();
    EXPECT_EQ(loader.devices(7)[0].ycPoints_[0].protocol_code, "123456789012:0x02010100");
}
}  // namespace

TEST(CfgLoader, RoutesFourPointCategoriesThroughNewBinding) {
    auto rows = configuration();
    CfgPointYxRow yx;
    yx.point_id = "stable-status";
    yx.owner_device_id = "rack";
    yx.binding_id = "binding";
    yx.code = "state";
    yx.address_space = "discrete_input";
    yx.wire_type = "bool";
    yx.invert = 1;
    rows.points_yx.push_back(yx);
    CfgPointYkRow yk;
    yk.point_id = "stable-start";
    yk.owner_device_id = "source";
    yk.binding_id = "binding";
    yk.code = "start";
    yk.address_space = "coil";
    yk.wire_type = "bool";
    yk.write_mode = "fixed";
    yk.fixed_raw_value = 1;
    rows.points_yk.push_back(yk);
    CfgPointYtRow yt;
    yt.point_id = "stable-setpoint";
    yt.owner_device_id = "source";
    yt.binding_id = "binding";
    yt.code = "power";
    yt.address_space = "holding_register";
    yt.wire_type = "f32";
    yt.register_count = 2;
    yt.scale_factor = 0.5;
    yt.offset_value = 10;
    rows.points_yt.push_back(yt);
    CfgLoader loader;
    ASSERT_TRUE(loader.configure(rows)) << loader.last_error();
    const auto devices = loader.devices(7);
    ASSERT_EQ(devices.size(), 2u);
    EXPECT_TRUE(devices[0].yxPoints_[0].invert);
    EXPECT_EQ(devices[0].yxPoints_[0].protocol_code, "42:2:0:8:0");
    EXPECT_EQ(devices[1].ykPoints_[0].protocol_code, "42:5:0:8:0");
    EXPECT_EQ(devices[1].ykPoints_[0].write_mode, "fixed");
    EXPECT_EQ(devices[1].ytPoints_[0].protocol_code, "42:16:0:4:0");
    EXPECT_DOUBLE_EQ(devices[1].ytPoints_[0].scale_factor, 0.5);
}

TEST(CfgLoader, Dlt698UsesBindingAddressAndRejectsConflictingSharedChannelAddresses) {
    auto rows = configuration();
    rows.channels[0].protocol = "dlt698";
    rows.points_yc[0].protocol_address_json = R"({"oad":"40000200"})";
    CfgLoader loader;
    ASSERT_TRUE(loader.configure(rows)) << loader.last_error();
    EXPECT_EQ(nlohmann::json::parse(loader.channels()[0].settings_)["meter_address"],
              "000000000042");
    auto source = rows.devices[1];
    source.id = 4;
    source.device_id = "other";
    rows.devices.push_back(source);
    auto binding = rows.bindings[0];
    binding.binding_id = "other-binding";
    binding.device_id = "other";
    binding.device_address = "43";
    rows.bindings.push_back(binding);
    EXPECT_FALSE(loader.configure(rows));
    EXPECT_EQ(loader.channels().size(), 1u);
}

TEST(CfgLoaderMySql, LoadsCurrentSchemaWithoutLegacyTables) {
    const auto* file = std::getenv("EMS_TEST_MYSQL_CONFIG");
    if (!file || !*file) GTEST_SKIP() << "Set EMS_TEST_MYSQL_CONFIG for live MySQL verification";
    auto options = ems::database::load_mysql_options(file);
    ASSERT_TRUE(ems::core::is_ok(options));
    auto configured = ems::core::value_of(options);
    configured.require_configuration_schema = true;
    const auto ctx = ems::core::make_call_context(std::chrono::seconds(10));
    auto database = ems::database::connect_mysql_database(configured, ctx);
    ASSERT_TRUE(ems::core::is_ok(database))
        << (ems::core::is_ok(database) ? "" : ems::core::error_of(database).message);
    CfgLoader loader;
    ASSERT_TRUE(loader.initialize(ems::core::value_of(database), ctx)) << loader.last_error();
    EXPECT_TRUE(loader.is_initialized());
}

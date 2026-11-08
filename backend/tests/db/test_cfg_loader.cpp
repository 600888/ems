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

DeviceConfigRecords forwarding_configuration() {
    auto records = configuration();
    auto channel = records.channels[0];
    channel.id = 8;
    channel.channel_id = "mms-output";
    channel.protocol = "iec61850_mms";
    channel.conn_mode = "tcp_server";
    channel.endpoint = "127.0.0.1:11002";
    channel.serial_json =
        R"({"iec61850":{"points":[{"code":"power","object_ref":"EMSLD0/MMXU1.TotW.mag.f","fc":"MX","mms_type":"float32","quality_ref":"EMSLD0/MMXU1.TotW.q","timestamp_ref":"EMSLD0/MMXU1.TotW.t"}]},"forwarding":[{"code":"power","source_point_id":"point-rack-voltage","stale_after_ms":5000}]})";
    records.channels.push_back(channel);
    auto binding = records.bindings[0];
    binding.id = 11;
    binding.binding_id = "output-binding";
    binding.channel_id = channel.channel_id;
    binding.can_collect = binding.can_control = 0;
    binding.can_forward = 1;
    records.bindings.push_back(binding);
    return records;
}

TEST(CfgLoader, CompilesForwardingSeparatelyAndKeepsDisabledSourcesInvalid) {
    auto records = forwarding_configuration();
    CfgLoader loader;
    ASSERT_TRUE(loader.configure(records)) << loader.last_error();
    ASSERT_EQ(loader.channels().size(), 1u);
    ASSERT_EQ(loader.forwarding_channels().size(), 1u);
    const auto output = loader.forwarding_channels()[0];
    ASSERT_EQ(output.points.size(), 1u);
    EXPECT_EQ(output.points[0].source_point_id, "point-rack-voltage");
    EXPECT_EQ(output.points[0].point_type, "yc");
    EXPECT_TRUE(output.points[0].source_enabled);
    const auto settings = nlohmann::json::parse(output.channel.settings_);
    EXPECT_FALSE(settings.contains("forwarding"));
    EXPECT_EQ(settings["iec61850"]["points"][0]["iec_quality"], 1);
    EXPECT_FALSE(settings["iec61850"]["points"][0].contains("value"));
    records.bindings[0].enabled = 0;
    ASSERT_TRUE(loader.configure(records)) << loader.last_error();
    EXPECT_FALSE(loader.forwarding_channels()[0].points[0].source_enabled);
    records.bindings[1].can_forward = 0;
    ASSERT_TRUE(loader.configure(records)) << loader.last_error();
    EXPECT_TRUE(loader.forwarding_channels().empty());
}

TEST(CfgLoader, RejectsInvalidForwardingMapsAndPreservesPreviousDirectory) {
    auto records = forwarding_configuration();
    CfgLoader loader;
    ASSERT_TRUE(loader.configure(records));
    auto options = nlohmann::json::parse(records.channels[1].serial_json);
    const auto valid = options;
    for (const auto& field : {"source", "missing_quality", "duplicate", "freshness", "unknown"}) {
        options = valid;
        if (field == std::string("source")) options["forwarding"][0]["source_point_id"] = "absent";
        if (field == std::string("missing_quality"))
            options["iec61850"]["points"][0].erase("quality_ref");
        if (field == std::string("duplicate"))
            options["forwarding"].push_back(options["forwarding"][0]);
        if (field == std::string("freshness")) options["forwarding"][0]["stale_after_ms"] = -1;
        if (field == std::string("unknown")) options["forwarding"][0]["scale"] = 10;
        records.channels[1].serial_json = options.dump();
        EXPECT_FALSE(loader.configure(records)) << field;
        EXPECT_EQ(loader.forwarding_channels().size(), 1u);
    }
    records.channels[1].serial_json = valid.dump();
    records.bindings[1].can_control = 1;
    EXPECT_FALSE(loader.configure(records));
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
    EXPECT_TRUE(loader.connection_scope(7).empty());  // 显式跨设备共享通道不再参与自动归并。
}

TEST(CfgLoader, ConnectionScopeUsesPhysicalDeviceAndComponentSource) {
    auto rows = configuration();
    CfgLoader loader;
    ASSERT_TRUE(loader.configure(rows)) << loader.last_error();
    EXPECT_EQ(loader.connection_scope(7), "source");
    EXPECT_TRUE(loader.connection_scope(999).empty());
    // rack 是 component，点的归属不应把它或站根变成连接复用边界。
    EXPECT_EQ(loader.devices(7)[0].deviceKey_, "rack");
    auto unit = rows.devices[0];
    unit.id = 10;
    unit.device_id = "unit";
    unit.node_kind = "unit";
    unit.parent_device_id = "station";
    rows.devices.push_back(unit);
    rows.devices[1].parent_device_id = "unit";
    ASSERT_TRUE(loader.configure(rows)) << loader.last_error();
    EXPECT_EQ(loader.connection_scope(7), "source");
    rows.points_yc[0].binding_id = "missing";
    EXPECT_FALSE(loader.configure(rows));
    EXPECT_EQ(loader.connection_scope(7), "source");
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

TEST(CfgLoader, KeepsImportedModbusWordOrderAndExplicitWriteFunction) {
    auto records = configuration();
    CfgLoader loader;
    const std::vector<std::string> orders = {"ABCD", "CDAB", "BADC", "DCBA"};
    for (std::size_t i = 0; i < orders.size(); ++i) {
        records.points_yc[0].byte_order = orders[i];
        ASSERT_TRUE(loader.configure(records)) << loader.last_error();
        EXPECT_EQ(loader.devices(7)[0].ycPoints_[0].protocol_code,
                  "42:3:12:4:" + std::to_string(i));
    }
    CfgPointYtRow point;
    point.point_id = "setpoint";
    point.owner_device_id = "rack";
    point.binding_id = "binding";
    point.code = "setpoint";
    point.address_space = "holding_register";
    point.address_offset = 50;
    point.wire_type = "u16";
    point.protocol_address_json = R"({"function_code":16})";
    records.points_yt.push_back(point);
    ASSERT_TRUE(loader.configure(records)) << loader.last_error();
    EXPECT_EQ(loader.devices(7)[0].ytPoints_[0].protocol_code, "42:16:50:0:0");
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

TEST(CfgLoader, Iec61850CompilesMmsDirectoryAndKeepsComponentIdentity) {
    auto rows = configuration();
    rows.channels[0].protocol = "iec61850_mms";
    rows.channels[0].endpoint = "127.0.0.1:10102";
    rows.channels[0].serial_json = R"({"iec61850":{"datasets":["SIMLD0/LLN0$measurements"]}})";
    rows.bindings[0].device_address = "LD-name-not-a-slave-number";
    rows.points_yc[0].protocol_address_json =
        R"({"object_ref":"SIMLD0/MMXU1.TotW.mag.f","fc":"MX","quality_ref":"SIMLD0/MMXU1.TotW.q"})";
    CfgPointYxRow yx;
    yx.point_id = "status";
    yx.owner_device_id = "rack";
    yx.binding_id = "binding";
    yx.wire_type = "bool";
    yx.protocol_address_json = R"({"object_ref":"SIMLD0/MMXU1.Status.stVal","fc":"ST"})";
    rows.points_yx.push_back(yx);
    CfgLoader loader;
    ASSERT_TRUE(loader.configure(rows)) << loader.last_error();
    const auto channel = loader.channels()[0];
    EXPECT_EQ(channel.protocol_, ems::model::ProtocolType::IEC61850);
    auto options = nlohmann::json::parse(channel.settings_)["iec61850"];
    ASSERT_EQ(options["points"].size(), 2u);
    EXPECT_EQ(options["points"][0]["code"], "MX:SIMLD0/MMXU1.TotW.mag.f");
    EXPECT_EQ(options["points"][0]["mms_type"], "float32");
    EXPECT_EQ(options["points"][1]["mms_type"], "boolean");
    const auto device = loader.devices(7)[0];
    EXPECT_EQ(device.deviceKey_, "rack");
    EXPECT_EQ(device.ycPoints_[0].point_id, "point-rack-voltage");
    EXPECT_DOUBLE_EQ(device.ycPoints_[0].scale_factor, 0.1);
    EXPECT_EQ(device.yxPoints_[0].protocol_code, "ST:SIMLD0/MMXU1.Status.stVal");
    rows.channels[0].protocol = "IEC61850";
    ASSERT_TRUE(loader.configure(rows)) << loader.last_error();
    rows.points_yc.push_back(rows.points_yc[0]);
    rows.points_yc.back().point_id = "same-source-alias";
    ASSERT_TRUE(loader.configure(rows)) << loader.last_error();
    EXPECT_EQ(nlohmann::json::parse(loader.channels()[0].settings_)["iec61850"]["points"].size(),
              2u);
    rows.points_yc.back().wire_type = "i32";
    EXPECT_FALSE(loader.configure(rows));
}

TEST(CfgLoader, Iec61850RejectsServerControlAndDirectoryOverrides) {
    auto rows = configuration();
    rows.channels[0].protocol = "iec61850_mms";
    rows.points_yc[0].protocol_address_json =
        R"({"object_ref":"SIMLD0/MMXU1.TotW.mag.f","fc":"MX"})";
    CfgLoader loader;
    ASSERT_TRUE(loader.configure(rows)) << loader.last_error();
    rows.channels[0].conn_mode = "tcp_server";
    EXPECT_FALSE(loader.configure(rows));
    rows.channels[0].conn_mode = "tcp_client";
    rows.channels[0].serial_json = R"({"iec61850":{"points":[]}})";
    EXPECT_FALSE(loader.configure(rows));
    rows.channels[0].serial_json.clear();
    CfgPointYkRow control;
    control.point_id = "command";
    control.owner_device_id = "source";
    control.binding_id = "binding";
    control.protocol_address_json =
        R"({"object_ref":"SIMLD0/CSWI1.Pos","fc":"CO","write_mode":"control"})";
    rows.points_yk.push_back(control);
    EXPECT_FALSE(loader.configure(rows));
    EXPECT_EQ(loader.channels().size(), 1u);
    EXPECT_EQ(loader.config_revision(), 7);
}

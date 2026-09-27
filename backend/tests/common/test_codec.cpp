/**
 * @file test_codec.cpp
 * @brief 编解码测试：64 位整数往返、无效值拒绝、事件信封（验收 C04 对应项）
 */
#include <common/codec/event_codec.h>
#include <common/codec/model_codec.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace {
namespace codec = ems::codec;
namespace core = ems::core;
namespace model = ems::model;
namespace messaging = ems::messaging;

template <typename T>
T expect_ok(const core::Result<T>& r) {
    if (!core::is_ok(r)) {
        ADD_FAILURE() << "期望成功，实际错误: " << core::error_of(r).message;
    }
    return core::is_ok(r) ? core::value_of(r) : T{};
}

// ---------------------------------------------------------------------------
// PointValue：64 位整数边界以十进制字符串往返，无浮点精度损失
// ---------------------------------------------------------------------------

TEST(PointValueCodec, Int64BoundariesRoundtrip) {
    for (std::int64_t v :
         {std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::min() + 1,
          std::int64_t{-1}, std::int64_t{0}, std::int64_t{1},
          std::numeric_limits<std::int64_t>::max() - 1, std::numeric_limits<std::int64_t>::max()}) {
        auto text =
            expect_ok(codec::encode_point_value(model::ValueType::Int64, model::PointValue{v}));
        // 64 位整数在 JSON 中必须是十进制字符串（防 JS 精度丢失）
        EXPECT_EQ(text.front(), '"') << text;
        EXPECT_EQ(text, "\"" + std::to_string(v) + "\"");
        auto back = expect_ok(codec::decode_point_value(model::ValueType::Int64, text));
        EXPECT_EQ(std::get<std::int64_t>(back), v);
    }
}

TEST(PointValueCodec, UInt64BoundariesRoundtrip) {
    for (std::uint64_t v :
         {std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint64_t>::max() - 1,
          std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{1} << 63}) {
        auto text =
            expect_ok(codec::encode_point_value(model::ValueType::UInt64, model::PointValue{v}));
        EXPECT_EQ(text, "\"" + std::to_string(v) + "\"");
        auto back = expect_ok(codec::decode_point_value(model::ValueType::UInt64, text));
        EXPECT_EQ(std::get<std::uint64_t>(back), v);
    }
}

TEST(PointValueCodec, BoolAndStringRoundtrip) {
    auto t = expect_ok(codec::encode_point_value(model::ValueType::Bool, model::PointValue{true}));
    EXPECT_EQ(t, "true");
    auto s = expect_ok(codec::encode_point_value(model::ValueType::String,
                                                 model::PointValue{std::string("中文值")}));
    auto back = expect_ok(codec::decode_point_value(model::ValueType::String, s));
    EXPECT_EQ(std::get<std::string>(back), "中文值");
}

TEST(PointValueCodec, DoubleRoundtripWithoutPrecisionLoss) {
    const double v = 1234567.8901234;
    auto text =
        expect_ok(codec::encode_point_value(model::ValueType::Double, model::PointValue{v}));
    auto back = expect_ok(codec::decode_point_value(model::ValueType::Double, text));
    EXPECT_DOUBLE_EQ(std::get<double>(back), v);
}

TEST(PointValueCodec, RejectsNaNAndInf) {
    auto r1 = codec::encode_point_value(model::ValueType::Double, model::PointValue{std::nan("")});
    EXPECT_FALSE(core::is_ok(r1));
    auto r2 = codec::encode_point_value(model::ValueType::Double, model::PointValue{HUGE_VAL});
    EXPECT_FALSE(core::is_ok(r2));
}

TEST(PointValueCodec, TypeMismatchIsSchemaMismatch) {
    // Int64 位置放 double：类型不符
    auto r = codec::encode_point_value(model::ValueType::Int64, model::PointValue{1.5});
    ASSERT_FALSE(core::is_ok(r));
    EXPECT_EQ(core::error_of(r).code, core::ErrorCode::InvalidArgument);

    // 解码：期望 Bool 得到字符串
    auto r2 = codec::decode_point_value(model::ValueType::Bool, "\"yes\"");
    ASSERT_FALSE(core::is_ok(r2));
    EXPECT_EQ(core::error_of(r2).code, core::ErrorCode::SchemaMismatch);
}

TEST(PointValueCodec, DecodeBadPayloadIsSchemaMismatch) {
    auto r = codec::decode_point_value(model::ValueType::Int64, "\"12abc\"");
    ASSERT_FALSE(core::is_ok(r));
    EXPECT_EQ(core::error_of(r).code, core::ErrorCode::SchemaMismatch);
}

// ---------------------------------------------------------------------------
// 模型 JSON 往返
// ---------------------------------------------------------------------------

TEST(ModelCodec, DeviceRoundtrip) {
    model::Device d;
    d.device_id = core::DeviceId::from_number(12345);
    d.asset_id = core::AssetId("d-1");
    d.code = "pcs01";
    d.name = "PCS 一号";
    d.type_version_id = "pcs-type-v1";
    d.model_version_id = "m-v2";
    d.vendor = "vendor-a";
    d.firmware_version = "1.2.3";
    d.enabled = false;

    auto text = expect_ok(codec::encode_device(d));
    auto back = expect_ok(codec::decode_device(text));
    EXPECT_EQ(back.device_id, d.device_id);
    EXPECT_EQ(back.asset_id, d.asset_id);
    EXPECT_EQ(back.code, d.code);
    EXPECT_EQ(back.name, d.name);
    EXPECT_FALSE(back.enabled);
}

TEST(ModelCodec, PointRoundtripWithFeedback) {
    model::Point p;
    p.point_id = core::PointId("p-fb");
    p.asset_id = core::AssetId("d-1");
    p.code = "pcs01.charge_fb";
    p.kind = model::PointKind::Status;
    p.value_type = model::ValueType::Bool;
    p.feedback_of = core::PointId("p-cmd");

    auto text = expect_ok(codec::encode_point(p));
    auto back = expect_ok(codec::decode_point(text));
    ASSERT_TRUE(back.feedback_of.has_value());
    EXPECT_EQ(*back.feedback_of, core::PointId("p-cmd"));
    EXPECT_EQ(back.kind, model::PointKind::Status);
}

TEST(ModelCodec, AssetRoundtripNullParent) {
    model::AssetNode a;
    a.asset_id = core::AssetId("st-1");
    a.station_id = core::StationId("st-1");
    a.code = "st01";
    a.asset_type = model::AssetType::Station;

    auto text = expect_ok(codec::encode_asset(a));
    auto back = expect_ok(codec::decode_asset(text));
    EXPECT_FALSE(back.parent_asset_id.has_value());  // 根节点 parent 为 null
    EXPECT_EQ(back.asset_type, model::AssetType::Station);
}

TEST(ModelCodec, BindingRoundtrip) {
    model::DeviceChannelBinding b;
    b.binding_id = "b-1";
    b.device_id = core::DeviceId("dev-1");
    b.channel_id = core::ChannelId("ch-1");
    b.operations = {model::BindingOp::Collect, model::BindingOp::Control};
    b.protocol_address = "1";
    b.version = "v1";

    auto text = expect_ok(codec::encode_binding(b));
    auto back = expect_ok(codec::decode_binding(text));
    ASSERT_EQ(back.operations.size(), 2u);
    EXPECT_EQ(back.operations[1], model::BindingOp::Control);
    EXPECT_EQ(back.protocol_address, "1");
}

TEST(ModelCodec, ImportedVendorRouteRoundtrip) {
    model::VendorPointRoute route;
    route.route_id = "point-table:pcs-1:power";
    route.point_id = core::PointId("point-active_power");
    route.binding_id = "binding-1";
    route.vendor_address = "modbus:holding_register:0";
    route.template_key = "power";
    route.vendor_name = "厂家功率";
    route.protocol = "modbus";
    route.address_space = "holding_register";
    route.address_offset = 0;
    route.wire_type = "f32";
    route.byte_order = "CDAB";
    route.scale = -0.001;

    auto text = expect_ok(codec::encode_vendor_route(route));
    auto decoded = expect_ok(codec::decode_vendor_route(text));
    EXPECT_EQ(decoded.template_key, route.template_key);
    EXPECT_EQ(decoded.vendor_name, route.vendor_name);
    EXPECT_EQ(decoded.address_space, route.address_space);
    EXPECT_EQ(decoded.address_offset, route.address_offset);
    EXPECT_EQ(decoded.wire_type, route.wire_type);
    EXPECT_EQ(decoded.byte_order, route.byte_order);
    EXPECT_EQ(decoded.scale, route.scale);
}

TEST(ModelCodec, ImportedVendorRouteRejectsBadOffsetType) {
    auto result = codec::decode_vendor_route(
        R"({"route_id":"r","point_id":"p","binding_id":"b","address_offset":"bad"})");
    ASSERT_FALSE(core::is_ok(result));
    EXPECT_EQ(core::error_of(result).code, core::ErrorCode::SchemaMismatch);
}

TEST(ModelCodec, ImportedVendorRouteRejectsBadScaleType) {
    auto result = codec::decode_vendor_route(
        R"({"route_id":"r","point_id":"p","binding_id":"b","scale":"bad"})");
    ASSERT_FALSE(core::is_ok(result));
    EXPECT_EQ(core::error_of(result).code, core::ErrorCode::SchemaMismatch);
}

TEST(ModelCodec, DecodeMissingRequiredFieldIsSchemaMismatch) {
    // 缺 device_id
    auto r = codec::decode_device("{\"asset_id\":\"d-1\"}");
    ASSERT_FALSE(core::is_ok(r));
    EXPECT_EQ(core::error_of(r).code, core::ErrorCode::SchemaMismatch);
}

TEST(ModelCodec, DecodeCorruptJsonIsSchemaMismatch) {
    auto r = codec::decode_device("not json");
    EXPECT_FALSE(core::is_ok(r));
    EXPECT_EQ(core::error_of(r).code, core::ErrorCode::SchemaMismatch);
}

// ---------------------------------------------------------------------------
// 事件信封
// ---------------------------------------------------------------------------

TEST(EventCodec, EnvelopeRoundtrip) {
    messaging::EventEnvelope e;
    e.schema_version = "1";
    e.event_type = messaging::EventType::Telemetry;
    e.event_id = core::EventId("evt-0001");
    e.trace_id = "tr-1";
    e.producer_id = "collector-01";
    e.station_id = core::StationId("st-1");
    e.unit_id = core::EssUnitId("u-1");
    e.asset_id = core::AssetId("d-1");
    e.device_path = {"pcs01"};
    e.config_version = "cv-7";
    e.event_time = 1726723200123456LL;
    e.ingest_time = 1726723200223456LL;
    e.payload_json = R"({"point_id":"p-pow","value":"-1500","quality":"Good"})";

    auto text = expect_ok(codec::encode_event(e));
    auto back = expect_ok(codec::decode_event(text));
    EXPECT_EQ(back.event_id, e.event_id);
    EXPECT_EQ(back.event_type, messaging::EventType::Telemetry);
    EXPECT_EQ(back.event_time, e.event_time);  // 64 位时间无损
    EXPECT_EQ(back.ingest_time, e.ingest_time);
    ASSERT_TRUE(back.station_id.has_value());
    EXPECT_EQ(*back.station_id, core::StationId("st-1"));
    ASSERT_TRUE(back.unit_id.has_value());
    ASSERT_EQ(back.device_path.size(), 1u);
    EXPECT_EQ(back.device_path[0], "pcs01");
    EXPECT_NE(back.payload_json.find("p-pow"), std::string::npos);
}

TEST(EventCodec, StationEventAllowsNullUnitAndEmptyPath) {
    messaging::EventEnvelope e;
    e.event_type = messaging::EventType::ConfigChanged;
    e.event_id = core::EventId("evt-0002");
    e.station_id = core::StationId("st-1");
    // unit 为 null、device_path 为 []
    auto text = expect_ok(codec::encode_event(e));
    auto back = expect_ok(codec::decode_event(text));
    EXPECT_FALSE(back.unit_id.has_value());
    EXPECT_TRUE(back.device_path.empty());
}

TEST(EventCodec, RejectsUnknownSchemaVersion) {
    // 未知版本必须隔离，不能反序列化为默认值继续执行
    messaging::EventEnvelope e;
    e.schema_version = "99";
    e.event_id = core::EventId("evt-0003");
    auto text = expect_ok(codec::encode_event(e));
    auto r = codec::decode_event(text);
    ASSERT_FALSE(core::is_ok(r));
    EXPECT_EQ(core::error_of(r).code, core::ErrorCode::SchemaMismatch);
}

TEST(EventCodec, RejectsUnknownEventType) {
    const std::string bad =
        R"({"schema_version":"1","event_type":"WarpDrive","event_id":"evt-0004"})";
    auto r = codec::decode_event(bad);
    ASSERT_FALSE(core::is_ok(r));
    EXPECT_EQ(core::error_of(r).code, core::ErrorCode::SchemaMismatch);
}

TEST(EventCodec, RejectsEmptyEventId) {
    const std::string bad = R"({"schema_version":"1","event_type":"Telemetry","event_id":""})";
    auto r = codec::decode_event(bad);
    ASSERT_FALSE(core::is_ok(r));
    EXPECT_EQ(core::error_of(r).code, core::ErrorCode::SchemaMismatch);
}

TEST(EventCodec, RejectsRemovedArchiveConfirmation) {
    const std::string removed =
        R"({"schema_version":"1","event_type":"ArchiveConfirmed","event_id":"evt-removed"})";
    auto r = codec::decode_event(removed);
    ASSERT_FALSE(core::is_ok(r));
    EXPECT_EQ(core::error_of(r).code, core::ErrorCode::SchemaMismatch);
}

TEST(EventCodec, EncodeRequiresEventId) {
    messaging::EventEnvelope e;  // event_id 为空
    auto r = codec::encode_event(e);
    ASSERT_FALSE(core::is_ok(r));
    EXPECT_EQ(core::error_of(r).code, core::ErrorCode::InvalidArgument);
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

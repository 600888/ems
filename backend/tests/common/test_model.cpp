/**
 * @file test_model.cpp
 * @brief 模型/校验/ModelView 测试（验收 C02/C03 对应项）
 */
#include <common/model/catalog.h>
#include <common/model/model_view.h>
#include <gtest/gtest.h>

#include <cmath>
#include <memory>

namespace ems::model {
namespace {

core::DeviceId did(const char* s) { return core::DeviceId(s); }

core::PointId pid(const char* s) { return core::PointId(s); }

core::ChannelId cid(const char* s) { return core::ChannelId(s); }

/// 构造一份合法目录：1 站 + 2 单元 + 设备（多通道 + 共享总线）+ 点/绑定/能力
ModelCatalog build_valid_catalog() {
    ModelCatalog c;

    // 设备树：站 → 单元 → 设备；两个单元同名 pcs01 不允许按 code 合并身份（C02）
    Device station;
    station.device_id = did("st-1");
    station.parent_device_id = did("");
    station.node_kind = DeviceNodeKind::Station;
    station.code = "st01";
    c.devices.push_back(station);

    for (const char* unit : {"u-1", "u-2"}) {
        Device u;
        u.device_id = did(unit);
        u.parent_device_id = did("st-1");
        u.node_kind = DeviceNodeKind::Unit;
        u.code = unit;
        c.devices.push_back(u);
    }

    Device d1;
    d1.device_id = did("dev-1");
    d1.parent_device_id = did("u-1");
    d1.node_kind = DeviceNodeKind::Device;
    d1.code = "pcs01";  // dev-2 也有同名 pcs01 设备，但 ID 不同
    d1.type_version_id = "pcs-type-v1";
    c.devices.push_back(d1);

    Device d2;
    d2.device_id = did("dev-2");
    d2.parent_device_id = did("u-2");
    d2.node_kind = DeviceNodeKind::Device;
    d2.code = "pcs01";
    d2.type_version_id = "pcs-type-v1";
    c.devices.push_back(d2);

    DeviceTypeDefinition type;
    type.type_version_id = "pcs-type-v1";
    type.type_code = "pcs";
    type.capability_codes = {"charge"};
    c.device_types.push_back(type);

    YkPoint cmd;
    cmd.point_id = pid("p-cmd");
    cmd.owner_device_id = did("dev-1");
    cmd.code = "charge_cmd";
    cmd.write_mode = WriteMode::Fixed;
    cmd.fixed_raw_value = 1;
    cmd.value_type = ValueType::Bool;
    c.add_point(cmd);

    YxPoint fb;
    fb.point_id = pid("p-fb");
    fb.owner_device_id = did("dev-1");
    fb.code = "charge_fb";
    fb.feedback_of = pid("p-cmd");  // 反馈点指回命令点
    c.add_point(fb);

    YcPoint tel;
    tel.point_id = pid("p-pow");
    tel.owner_device_id = did("dev-1");
    tel.code = "active_power";
    tel.unit = "kW";
    c.add_point(tel);

    ChannelConfig ch1;
    ch1.channel_id = cid("ch-1");
    ch1.station_id = core::StationId("st-1");
    ch1.code = "ch01";
    c.channels.push_back(ch1);

    ChannelConfig ch2;
    ch2.channel_id = cid("ch-2");
    ch2.station_id = core::StationId("st-1");
    ch2.code = "ch02";
    c.channels.push_back(ch2);

    // dev-1 多通道（ch-1 + ch-2），ch-1 共享总线挂 dev-1/dev-2（C02 多对多）
    DeviceChannelBinding b1;
    b1.binding_id = "b-1";
    b1.device_id = did("dev-1");
    b1.channel_id = cid("ch-1");
    b1.operations = {BindingOp::Collect, BindingOp::Control};
    b1.protocol_address = "1";
    c.bindings.push_back(b1);

    DeviceChannelBinding b2;
    b2.binding_id = "b-2";
    b2.device_id = did("dev-1");
    b2.channel_id = cid("ch-2");
    b2.operations = {BindingOp::Collect};
    b2.protocol_address = "1";
    c.bindings.push_back(b2);

    DeviceChannelBinding b3;
    b3.binding_id = "b-3";
    b3.device_id = did("dev-2");
    b3.channel_id = cid("ch-1");
    b3.operations = {BindingOp::Collect};
    b3.protocol_address = "2";  // 共享总线地址属于绑定
    c.bindings.push_back(b3);

    VendorPointRoute route;
    route.route_id = "r-1";
    route.point_id = pid("p-pow");
    route.binding_id = "b-1";
    route.vendor_address = "40001";
    c.vendor_routes.push_back(route);

    CapabilityDefinition cap;
    cap.capability_code = "charge";
    cap.required_points = {pid("p-pow")};
    cap.required_feedbacks = {pid("p-fb")};
    c.capabilities.push_back(cap);

    return c;
}

std::vector<std::string> issue_codes(const ValidationReport& r) {
    std::vector<std::string> codes;
    for (const auto& i : r.issues) codes.push_back(i.code);
    return codes;
}

bool has_code(const ValidationReport& r, const char* code) {
    for (const auto& i : r.issues) {
        if (i.code == code) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 合法目录：索引与全部查询
// ---------------------------------------------------------------------------

TEST(ModelCatalog, ValidCatalogPasses) {
    auto c = build_valid_catalog();
    auto report = c.build_indexes();
    EXPECT_TRUE(report.ok()) << (report.issues.empty() ? "" : report.issues[0].reason);
}

TEST(ModelCatalog, FindOperations) {
    auto c = build_valid_catalog();
    c.build_indexes();
    EXPECT_NE(c.find_device(did("dev-1")), nullptr);
    EXPECT_EQ(c.find_device(did("dev-9")), nullptr);
    EXPECT_NE(c.find_point(pid("p-pow")), nullptr);
    EXPECT_NE(c.find_channel(cid("ch-1")), nullptr);
    EXPECT_NE(c.find_binding("b-1"), nullptr);
    EXPECT_EQ(c.find_binding("b-9"), nullptr);
}

TEST(ModelCatalog, SameCodeDifferentDevicesNotMerged) {
    // C02：多单元同名 pcs01，不按全局 device_code 错误合并
    auto c = build_valid_catalog();
    c.build_indexes();
    const Device* d1 = c.find_device(did("dev-1"));
    const Device* d2 = c.find_device(did("dev-2"));
    ASSERT_NE(d1, nullptr);
    ASSERT_NE(d2, nullptr);
    EXPECT_EQ(d1->code, d2->code);            // code 相同
    EXPECT_NE(d1->device_id, d2->device_id);  // 身份独立
}

TEST(ModelCatalog, MultiChannelAndSharedBusBindings) {
    // C02：同设备多通道 + 多设备共享总线
    auto c = build_valid_catalog();
    c.build_indexes();
    auto all = c.bindings_of_device(did("dev-1"));
    EXPECT_EQ(all.size(), 2u);  // ch-1 + ch-2
    auto collect = c.bindings_of_device(did("dev-1"), BindingOp::Collect);
    EXPECT_EQ(collect.size(), 2u);
    auto control = c.bindings_of_device(did("dev-1"), BindingOp::Control);
    EXPECT_EQ(control.size(), 1u);
    auto dev2 = c.bindings_of_device(did("dev-2"));
    EXPECT_EQ(dev2.size(), 1u);
    EXPECT_EQ(dev2[0]->protocol_address, "2");  // 共享总线上各自地址
    // 无此操作绑定 → 空列表
    EXPECT_TRUE(c.bindings_of_device(did("dev-2"), BindingOp::Control).empty());
}

// ---------------------------------------------------------------------------
// 校验：身份唯一 / 设备树环与层级 / 引用 / 绑定 / 反馈 / 路由（C03）
// ---------------------------------------------------------------------------

TEST(ModelCatalog, DuplicateIdentityReported) {
    auto c = build_valid_catalog();
    Device dup = c.devices.front();
    c.devices.push_back(dup);  // 重复 device_id
    auto r = c.build_indexes();
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(has_code(r, validation_code::kDuplicateIdentity));
}

TEST(ModelCatalog, DeviceCycleReported) {
    auto c = build_valid_catalog();
    c.devices[3].parent_device_id = did("dev-2");  // dev-1 → dev-2 → dev-1 成环
    c.devices[4].parent_device_id = did("dev-1");
    auto r = c.build_indexes();
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(has_code(r, validation_code::kDeviceCycle));
}

TEST(ModelCatalog, ParentKindRulesEnforced) {
    auto c = build_valid_catalog();
    c.devices[1].parent_device_id = did("u-2");  // 单元不能挂在单元下
    // 设备不能挂在部件下：补一个部件节点作为非法父节点
    Device rack;
    rack.device_id = did("rack-1");
    rack.parent_device_id = did("u-1");
    rack.node_kind = DeviceNodeKind::Component;
    rack.code = "rack01";
    rack.type_version_id = "battery-v1";
    c.devices.push_back(rack);
    c.devices[3].parent_device_id = did("rack-1");
    auto r = c.build_indexes();
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(has_code(r, validation_code::kInvalidValue));
}

TEST(ModelCatalog, UnitRequiresStationParent) {
    auto c = build_valid_catalog();
    c.devices[1].parent_device_id = did("");  // 只有站根可以没有父节点
    auto r = c.build_indexes();
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(has_code(r, validation_code::kInvalidValue));
}

TEST(ModelCatalog, MissingReferenceReported) {
    auto c = build_valid_catalog();
    c.points_yc[0].owner_device_id = did("ghost-node");
    auto r = c.build_indexes();
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(has_code(r, validation_code::kMissingReference));
}

TEST(ModelCatalog, FindStationRootWalksParentChain) {
    auto c = build_valid_catalog();
    c.build_indexes();
    const Device* root = c.find_station_root(did("dev-1"));
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->device_id, did("st-1"));
    EXPECT_EQ(root->node_kind, DeviceNodeKind::Station);
    EXPECT_EQ(c.find_station_root(did("ghost-node")), nullptr);
}

TEST(ModelCatalog, DuplicateBindingReported) {
    auto c = build_valid_catalog();
    DeviceChannelBinding dup = c.bindings[0];
    dup.binding_id = "b-dup";
    c.bindings.push_back(dup);  // 同设备同通道重复绑定
    auto r = c.build_indexes();
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(has_code(r, validation_code::kDuplicateBinding));
}

TEST(ModelCatalog, FourRemoteTypesAreDistinct) {
    auto c = build_valid_catalog();
    ASSERT_EQ(c.points_yk.size(), 1u);
    ASSERT_EQ(c.points_yx.size(), 1u);
    ASSERT_EQ(c.points_yc.size(), 1u);
    ASSERT_EQ(c.points_yt.size(), 0u);
    EXPECT_EQ(c.point_count(), 3u);

    // 遥控与遥调都存为 Command，但四遥分类必须各自保留
    EXPECT_EQ(c.points_yk[0].point_type, PointType::YK);
    EXPECT_EQ(c.points_yk[0].kind, PointKind::Command);
    EXPECT_TRUE(is_command(c.points_yk[0].point_type));
    EXPECT_EQ(c.points_yx[0].point_type, PointType::YX);
    EXPECT_EQ(c.points_yx[0].kind, PointKind::Status);
    EXPECT_TRUE(is_measured(c.points_yx[0].point_type));

    YtPoint yt;
    yt.point_id = pid("p-set");
    yt.owner_device_id = did("dev-1");
    yt.code = "set_active_power";
    yt.scale_factor = 0.1;
    c.add_point(yt);
    EXPECT_EQ(c.points_yt[0].kind, PointKind::Command);
    EXPECT_EQ(c.points_yt[0].point_type, PointType::YT);
    EXPECT_TRUE(c.build_indexes().ok());
    ASSERT_NE(c.find_point(pid("p-set")), nullptr);
    EXPECT_EQ(c.find_point(pid("p-set"))->point_type, PointType::YT);
}

TEST(ModelCatalog, MissingChannelReferenceReported) {
    auto c = build_valid_catalog();
    c.bindings[0].channel_id = cid("ch-ghost");
    auto r = c.build_indexes();
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(has_code(r, validation_code::kMissingReference));
}

TEST(ModelCatalog, InvalidFeedbackRefReported) {
    auto c = build_valid_catalog();
    c.points_yx[0].feedback_of = pid("p-pow");  // 反馈引用遥测点
    auto r = c.build_indexes();
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(has_code(r, validation_code::kInvalidFeedbackRef));
}

TEST(ModelCatalog, MissingFeedbackPointReported) {
    auto c = build_valid_catalog();
    c.points_yx[0].feedback_of = pid("p-ghost");
    auto r = c.build_indexes();
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(has_code(r, validation_code::kInvalidFeedbackRef));
}

TEST(ModelCatalog, MissingVersionReported) {
    auto c = build_valid_catalog();
    c.devices[3].type_version_id = "ghost-type";
    auto r = c.build_indexes();
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(has_code(r, validation_code::kVersionMismatch));
}

TEST(ModelCatalog, DuplicateRouteReported) {
    auto c = build_valid_catalog();
    VendorPointRoute dup = c.vendor_routes[0];
    dup.route_id = "r-dup";
    c.vendor_routes.push_back(dup);
    auto r = c.build_indexes();
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(has_code(r, validation_code::kDuplicateRoute));
}

TEST(ModelCatalog, ReportCarriesObjectAndFieldAndReason) {
    auto c = build_valid_catalog();
    c.devices[3].parent_device_id = did("ghost-node");
    auto r = c.build_indexes();
    ASSERT_FALSE(r.issues.empty());
    const auto& issue = r.issues[0];
    EXPECT_FALSE(issue.code.empty());
    EXPECT_EQ(issue.object_id, "dev-1");
    EXPECT_EQ(issue.field_path, "parent_device_id");
    EXPECT_FALSE(issue.reason.empty());
}

// ---------------------------------------------------------------------------
// value_matches_type（无效值不变成 0）
// ---------------------------------------------------------------------------

TEST(ModelValueType, MatchesDeclaredType) {
    EXPECT_TRUE(value_matches_type(model::PointValue{std::int64_t(5)}, ValueType::Int64));
    EXPECT_FALSE(value_matches_type(model::PointValue{std::int64_t(5)}, ValueType::Double));
    EXPECT_TRUE(value_matches_type(model::PointValue{std::uint64_t(5)}, ValueType::UInt64));
    EXPECT_TRUE(value_matches_type(model::PointValue{3.5}, ValueType::Double));
    EXPECT_TRUE(value_matches_type(model::PointValue{std::string("x")}, ValueType::String));
    EXPECT_FALSE(value_matches_type(model::PointValue{std::string("x")}, ValueType::Bool));
}

TEST(ModelValueType, RejectsNaNAndInf) {
    EXPECT_FALSE(value_matches_type(model::PointValue{std::nan("")}, ValueType::Double));
    EXPECT_FALSE(value_matches_type(model::PointValue{HUGE_VAL}, ValueType::Double));
    EXPECT_FALSE(value_matches_type(model::PointValue{-HUGE_VAL}, ValueType::Double));
}

// ---------------------------------------------------------------------------
// ModelView（6.3 查询接口）
// ---------------------------------------------------------------------------

TEST(ModelView, FindDeviceAndPoint) {
    auto c = std::make_shared<ModelCatalog>(build_valid_catalog());
    c->build_indexes();
    ModelView view(c);
    const Device* d = view.find_device(did("dev-1"));
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->code, "pcs01");
    EXPECT_EQ(view.find_device(did("dev-9")), nullptr);  // NotFound，不补齐
    EXPECT_NE(view.find_point(pid("p-pow")), nullptr);
}

TEST(ModelView, ListBindingsEmptyMeansNoBinding) {
    auto c = std::make_shared<ModelCatalog>(build_valid_catalog());
    c->build_indexes();
    ModelView view(c);
    EXPECT_EQ(view.list_bindings(did("dev-1"), BindingOp::Control).size(), 1u);
    EXPECT_TRUE(view.list_bindings(did("dev-2"), BindingOp::Control).empty());
    EXPECT_TRUE(view.list_bindings(did("dev-ghost")).empty());
}

TEST(ModelView, ResolveCapabilityOk) {
    auto c = std::make_shared<ModelCatalog>(build_valid_catalog());
    c->build_indexes();
    ModelView view(c);
    auto res = view.resolve_capability(did("dev-1"), "charge");
    ASSERT_TRUE(res.ok) << res.reason;
    EXPECT_NE(res.capability, nullptr);
    EXPECT_EQ(res.required_points.size(), 1u);
    EXPECT_EQ(res.required_feedbacks.size(), 1u);
}

TEST(ModelView, ResolveCapabilityMissingInType) {
    auto c = std::make_shared<ModelCatalog>(build_valid_catalog());
    c->build_indexes();
    ModelView view(c);
    auto res = view.resolve_capability(did("dev-1"), "fire_sale");
    EXPECT_FALSE(res.ok);
    EXPECT_NE(res.reason.find("未声明"), std::string::npos);
}

TEST(ModelView, ResolveCapabilityDisabled) {
    auto c = build_valid_catalog();
    c.capabilities[0].enabled = false;
    auto shared = std::make_shared<ModelCatalog>(std::move(c));
    shared->build_indexes();
    ModelView view(shared);
    auto res = view.resolve_capability(did("dev-1"), "charge");
    EXPECT_FALSE(res.ok);
    EXPECT_NE(res.reason.find("禁用"), std::string::npos);
}

TEST(ModelView, ResolveCapabilityDisabledPoint) {
    auto c = build_valid_catalog();
    c.points_yc[0].enabled = false;  // p-pow 禁用
    auto shared = std::make_shared<ModelCatalog>(std::move(c));
    shared->build_indexes();
    ModelView view(shared);
    auto res = view.resolve_capability(did("dev-1"), "charge");
    EXPECT_FALSE(res.ok);
    EXPECT_NE(res.reason.find("禁用"), std::string::npos);
}

TEST(ModelView, ResolveCapabilityUnknownDevice) {
    auto c = std::make_shared<ModelCatalog>(build_valid_catalog());
    c->build_indexes();
    ModelView view(c);
    auto res = view.resolve_capability(did("dev-ghost"), "charge");
    EXPECT_FALSE(res.ok);
}

}  // namespace
}  // namespace ems::model

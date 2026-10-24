#include <common/model/device_models.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <memory>

namespace ems::model {
namespace {

bool has_issue(const ValidationReport& report, const char* code) {
    for (const auto& issue : report.issues) {
        if (issue.code == code) return true;
    }
    return false;
}

struct Fixture {
    ModelCatalog catalog;
    DevicePointDraft draft;

    Fixture() {
        AssetNode asset;
        asset.asset_id = core::AssetId("asset-pcs");
        asset.station_id = core::StationId("station-1");
        asset.code = "pcs01";
        asset.asset_type = AssetType::Device;
        catalog.assets.push_back(asset);

        PcsDeviceModel model;
        auto type = model.definition();
        for (const auto& definition : type.standard_points) {
            auto point = make_standard_point(
                definition, core::PointId("point-" + definition.semantic_code), asset.asset_id,
                definition.semantic_code);
            const core::PointId point_id = as_base(point).point_id;
            catalog.add_point(std::move(point));

            DeviceStandardPointConfig config;
            config.device_id = core::DeviceId("device-1");
            config.point_id = point_id;
            config.standard_code = definition.semantic_code;
            config.state = StandardPointState::Disabled;
            config.disabled_reason = "unsupported";
            draft.points.push_back(config);
        }
        catalog.device_types.push_back(std::move(type));

        Device device;
        device.device_id = core::DeviceId("device-1");
        device.asset_id = asset.asset_id;
        device.type_version_id = "pcs-v1";
        catalog.devices.push_back(device);
        draft.device_id = device.device_id;
        draft.capabilities = model.capabilities();

        ChannelConfig channel;
        channel.channel_id = core::ChannelId("channel-1");
        channel.station_id = asset.station_id;
        channel.purpose = ChannelPurpose::Both;
        catalog.channels.push_back(channel);
        DeviceChannelBinding binding;
        binding.binding_id = "binding-1";
        binding.device_id = device.device_id;
        binding.channel_id = channel.channel_id;
        binding.operations = {BindingOp::Collect, BindingOp::Control};
        catalog.bindings.push_back(binding);
        catalog.build_indexes();
    }

    DeviceStandardPointConfig& config(const char* code) {
        for (auto& point : draft.points) {
            if (point.standard_code == code) return point;
        }
        std::abort();
    }

    void enable(const char* code, bool writable = false) {
        auto& point = config(code);
        point.state = StandardPointState::Enabled;
        point.disabled_reason.clear();
        VendorPointRoute route;
        route.route_id = std::string("route-") + code;
        route.point_id = point.point_id;
        route.binding_id = "binding-1";
        route.vendor_address = code;
        route.writable = writable;
        catalog.vendor_routes.push_back(route);
        catalog.build_indexes();
    }
};

TEST(DeviceConfig, PcsCatalogContainsExpandedCodes) {
    PcsDeviceModel model;
    auto type = model.definition();
    EXPECT_EQ(type.type_code, "pcs");
    EXPECT_EQ(type.standard_points.size(), 28u);
    EXPECT_TRUE(std::any_of(type.standard_points.begin(), type.standard_points.end(),
                            [](const StandardPointDefinition& point) {
                                return point.semantic_code == "ac_voltage_ca";
                            }));
}

TEST(DeviceModels, BaseCarriesOnlyCommonMetadata) {
    std::unique_ptr<DeviceModel> device = std::make_unique<PcsDeviceModel>();
    EXPECT_EQ(device->type_code(), "pcs");
    EXPECT_EQ(device->type_version_id(), "pcs-v1");
    EXPECT_EQ(device->name(), "储能变流器");
}

TEST(DeviceModels, SubclassesDefineTheirOwnPointsAndCapabilities) {
    PcsDeviceModel pcs;
    BmsDeviceModel bms;
    FireDeviceModel fire;
    const auto bms_type = bms.definition();
    const auto fire_type = fire.definition();
    EXPECT_EQ(pcs.definition().standard_points.size(), 28u);
    EXPECT_EQ(bms_type.type_code, "bms");
    EXPECT_EQ(fire_type.type_code, "fire");
    EXPECT_TRUE(std::any_of(
        bms_type.standard_points.begin(), bms_type.standard_points.end(),
        [](const StandardPointDefinition& point) { return point.semantic_code == "soc"; }));
    EXPECT_TRUE(std::any_of(
        fire_type.standard_points.begin(), fire_type.standard_points.end(),
        [](const StandardPointDefinition& point) { return point.semantic_code == "fire_alarm"; }));
    EXPECT_EQ(bms.capabilities().front().code, "read_soc");
    EXPECT_EQ(fire.capabilities().front().code, "monitor_fire");
}

TEST(DeviceConfig, DisabledPointsCanPublishWithReason) {
    Fixture fixture;
    EXPECT_TRUE(validate_device_point_draft(fixture.catalog, fixture.draft, true).ok());
    fixture.draft.points.front().disabled_reason.clear();
    EXPECT_TRUE(has_issue(validate_device_point_draft(fixture.catalog, fixture.draft, true),
                          validation_code::kInvalidValue));
}

TEST(DeviceConfig, UnconfiguredPointCannotPublish) {
    Fixture fixture;
    fixture.config("active_power").state = StandardPointState::Unconfigured;
    EXPECT_TRUE(validate_device_point_draft(fixture.catalog, fixture.draft, false).ok());
    EXPECT_FALSE(validate_device_point_draft(fixture.catalog, fixture.draft, true).ok());
}

TEST(DeviceConfig, EnabledPointRequiresCorrectRouteAndDevice) {
    Fixture fixture;
    fixture.enable("active_power");
    EXPECT_TRUE(validate_device_point_draft(fixture.catalog, fixture.draft, true).ok());
    fixture.catalog.vendor_routes[0].writable = true;
    fixture.catalog.build_indexes();
    EXPECT_FALSE(validate_device_point_draft(fixture.catalog, fixture.draft, true).ok());
    fixture.catalog.vendor_routes[0].writable = false;
    fixture.catalog.vendor_routes.push_back(fixture.catalog.vendor_routes[0]);
    fixture.catalog.vendor_routes.back().route_id = "route-duplicate";
    fixture.catalog.build_indexes();
    EXPECT_TRUE(has_issue(validate_device_point_draft(fixture.catalog, fixture.draft, true),
                          validation_code::kDuplicateRoute));
}

TEST(DeviceConfig, CommandRequiresControlChannel) {
    Fixture fixture;
    fixture.enable("start", true);
    EXPECT_TRUE(validate_device_point_draft(fixture.catalog, fixture.draft, true).ok());
    fixture.catalog.channels[0].purpose = ChannelPurpose::Telemetry;
    fixture.catalog.build_indexes();
    EXPECT_FALSE(validate_device_point_draft(fixture.catalog, fixture.draft, true).ok());
}

TEST(DeviceConfig, RejectsWrongDefinitionAndInstance) {
    Fixture fixture;
    fixture.config("active_power").device_id = core::DeviceId("other-device");
    fixture.catalog.points_yc[0].unit = "W";
    fixture.catalog.build_indexes();
    auto report = validate_device_point_draft(fixture.catalog, fixture.draft, true);
    EXPECT_TRUE(has_issue(report, validation_code::kMissingReference));
    EXPECT_TRUE(has_issue(report, validation_code::kInvalidValue));
}

TEST(DeviceConfig, DisabledPointCannotRetainActiveProjection) {
    Fixture fixture;
    fixture.enable("active_power");
    auto& config = fixture.config("active_power");
    config.state = StandardPointState::Disabled;
    config.disabled_reason = "not_used";
    EXPECT_FALSE(validate_device_point_draft(fixture.catalog, fixture.draft, true).ok());
}

TEST(DeviceConfig, CapabilitiesExplainDisabledDependencies) {
    Fixture fixture;
    auto status = resolve_standard_capability(fixture.draft, "monitor_power");
    EXPECT_FALSE(status.available);
    ASSERT_EQ(status.reasons.size(), 1u);
    EXPECT_NE(status.reasons.front().find("active_power"), std::string::npos);
    fixture.enable("active_power");
    EXPECT_TRUE(resolve_standard_capability(fixture.draft, "monitor_power").available);
}

}  // namespace
}  // namespace ems::model

/**
 * @file point_import_caller.cpp
 * @brief 采集数据导入调用器实现
 */
#include "point_import_caller.h"

#include <chrono>
#include <ctime>
#include <sstream>

#include "config_types.h"
#include "net/protocol/protocol_types.h"

namespace ems {
namespace net {
namespace device {

PointImportCaller::PointImportCaller() : dbm_(std::make_unique<ems::db::DbManager>()) {}

PointImportCaller::~PointImportCaller() = default;

bool PointImportCaller::initialize(const ems::db::DbConfig& config) {
    if (initialized_) return true;

    bool ok = dbm_->connect(config);
    if (ok) {
        initialized_ = true;
        importer_ = std::make_unique<ems::db::PointImporter>(*dbm_);
    }
    return ok;
}

int PointImportCaller::import_collected(const std::vector<ems::db::CollectImportData>& data_list) {
    if (!initialized_ || !importer_) return enum_value(protocol::OperationResult::Failure);
    return importer_->import_collected(data_list);
}

namespace {
std::string now_string() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&tt);
    char buf[enum_value(ImportLimits::TimestampTextBytes)];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}
}  // anonymous namespace

int PointImportCaller::import_yc_batch(
    int64_t channel_id, int64_t device_id,
    const std::vector<std::tuple<std::string, int64_t, double>>& values,
    const std::string& collect_time) {
    if (!initialized_ || !importer_) return enum_value(protocol::OperationResult::Failure);

    std::vector<ems::db::CollectImportData> list;
    list.reserve(values.size());

    std::string ts = collect_time.empty() ? now_string() : collect_time;
    for (const auto& [code, raw, eng] : values) {
        list.push_back(ems::db::PointImporter::make_yc_data(
            code, channel_id, device_id, raw, eng, enum_value(model::PointQuality::Good), ts));
    }

    return importer_->import_collected(list);
}

int PointImportCaller::import_yx_batch(int64_t channel_id, int64_t device_id,
                                       const std::vector<std::pair<std::string, int>>& values,
                                       const std::string& collect_time) {
    if (!initialized_ || !importer_) return enum_value(protocol::OperationResult::Failure);

    std::vector<ems::db::CollectImportData> list;
    list.reserve(values.size());

    std::string ts = collect_time.empty() ? now_string() : collect_time;
    for (const auto& [code, val] : values) {
        list.push_back(ems::db::PointImporter::make_yx_data(code, channel_id, device_id, val, ts));
    }

    return importer_->import_collected(list);
}

}  // namespace device
}  // namespace net
}  // namespace ems

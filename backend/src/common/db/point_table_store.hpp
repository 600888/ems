#pragma once

#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "db/channel.h"
#include "db/device.h"
#include "db/point_table.h"

namespace ems::db {

/**
 * @brief 在已连接的 ormpp 数据库上将整表配置原子写入四遥表。
 * @param[in,out] database 由调用方独占使用的数据库连接。
 * @param[in,out] result 已通过解析的草稿；成功后写入新点 ID 和提交状态。
 * @param[in] channel_id 选定通道 ID。
 * @param[in] device_id 选定设备 ID。
 * @param[in] mode 导入模式。
 * @param[in] mysql 是否采用 MySQL 行锁。
 * @return true 表示事务已提交；失败时回滚并填入错误。
 */
template <typename Database>
bool store_point_table(Database& database, PointTableImportResult& result, int64_t channel_id,
                       int64_t device_id, PointTableImportMode mode, bool mysql) {
    bool transaction = false;
    auto check = [&]() {
        if (database.has_error()) throw std::runtime_error(database.get_last_error());
    };
    try {
        if (!database.begin()) throw std::runtime_error(database.get_last_error());
        transaction = true;
        // MySQL 串行化点表导入，防止不同通道同时通过跨四遥表的编码唯一性检查。
        if (mysql) {
            database.template query_s<channel>("1=1 ORDER BY id FOR UPDATE");
            check();
        }
        const auto channels = database.template query_s<channel>("id=?", channel_id);
        check();
        const auto devices =
            database.template query_s<device>(mysql ? "id=? FOR UPDATE" : "id=?", device_id);
        check();
        if (channels.size() != 1 || devices.size() != 1 || channels[0].device_id != device_id) {
            throw std::runtime_error("设备或通道不存在，或通道不属于选定设备");
        }
        auto all_yc = database.template query_s<point_yc>();
        check();
        auto all_yx = database.template query_s<point_yx>();
        check();
        auto all_yk = database.template query_s<point_yk>();
        check();
        auto all_yt = database.template query_s<point_yt>();
        check();
        auto selected = [&](const auto& point) {
            return point.channel_id == channel_id && point.device_id == device_id;
        };
        std::unordered_map<std::string, int64_t> yc_ids, yx_ids;
        std::unordered_set<int64_t> removed_yc, removed_yx;
        std::unordered_set<std::string> existing;
        auto gather = [&](const auto& points, auto& ids, auto& removed) {
            for (const auto& point : points) {
                if (selected(point) && mode == PointTableImportMode::Replace)
                    removed.insert(point.id);
                else
                    existing.insert(point.code);
                if (selected(point) && mode == PointTableImportMode::Append)
                    ids[point.code] = point.id;
            }
        };
        gather(all_yc, yc_ids, removed_yc);
        gather(all_yx, yx_ids, removed_yx);
        for (const auto& p : all_yk) {
            if (!selected(p) && removed_yx.count(p.related_yx_id))
                throw std::runtime_error("其他通道遥控引用本次替换的遥信点");
            if (!selected(p) || mode == PointTableImportMode::Append) existing.insert(p.code);
        }
        for (const auto& p : all_yt) {
            if (!selected(p) && removed_yc.count(p.related_yc_id))
                throw std::runtime_error("其他通道遥调引用本次替换的遥测点");
            if (!selected(p) || mode == PointTableImportMode::Append) existing.insert(p.code);
        }
        auto unique = [&](const auto& points) {
            for (const auto& point : points) {
                if (point.protocol_type != channels[0].protocol_type)
                    throw std::runtime_error("解析后通道协议已变化，请重新导入");
                if (!existing.insert(point.code).second)
                    throw std::runtime_error("测点编码已存在：" + point.code);
            }
        };
        unique(result.data.yc);
        unique(result.data.yx);
        unique(result.data.yk);
        unique(result.data.yt);
        if (mode == PointTableImportMode::Replace) {
            // 只删除本设备及通道的配置，先删命令再删反馈点。
            database.template delete_records_s<point_yk>("channel_id=? AND device_id=?", channel_id,
                                                         device_id);
            check();
            database.template delete_records_s<point_yt>("channel_id=? AND device_id=?", channel_id,
                                                         device_id);
            check();
            database.template delete_records_s<point_yx>("channel_id=? AND device_id=?", channel_id,
                                                         device_id);
            check();
            database.template delete_records_s<point_yc>("channel_id=? AND device_id=?", channel_id,
                                                         device_id);
            check();
        }
        const auto now = std::time(nullptr);
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        std::ostringstream stream;
        stream << std::put_time(&utc, "%Y-%m-%d %H:%M:%S");
        auto insert = [&](auto& points, auto& ids) {
            for (auto& point : points) {
                point.created_at = point.updated_at = stream.str();
                const auto id = database.get_insert_id_after_insert(point);
                check();
                if (id == 0) throw std::runtime_error("测点写入失败：" + point.code);
                point.id = static_cast<int64_t>(id);
                ids[point.code] = point.id;
            }
        };
        insert(result.data.yc, yc_ids);
        insert(result.data.yx, yx_ids);
        for (auto& point : result.data.yk) {
            const auto code = result.data.yk_feedback.at(point.code);
            if (!code.empty()) {
                if (!yx_ids.count(code))
                    throw std::runtime_error("关联遥信不在选定设备及通道：" + code);
                point.related_yx_id = yx_ids.at(code);
            }
        }
        for (auto& point : result.data.yt) {
            const auto code = result.data.yt_feedback.at(point.code);
            if (!code.empty()) {
                if (!yc_ids.count(code))
                    throw std::runtime_error("关联遥测不在选定设备及通道：" + code);
                point.related_yc_id = yc_ids.at(code);
            }
        }
        std::unordered_map<std::string, int64_t> command_ids;
        insert(result.data.yk, command_ids);
        insert(result.data.yt, command_ids);
        if (!database.commit()) throw std::runtime_error(database.get_last_error());
        transaction = false;
        result.inserted = result.data.yc.size() + result.data.yx.size() + result.data.yk.size() +
                          result.data.yt.size();
        result.committed = true;
        return true;
    } catch (const std::exception& e) {
        const auto reason = std::string(e.what());
        if (transaction) database.rollback();
        result.errors.push_back({{}, 0, "database", reason});
        // 回滚后的 ID 不再代表数据库记录。
        auto clear = [](auto& points) {
            for (auto& p : points) p.id = 0;
        };
        clear(result.data.yc);
        clear(result.data.yx);
        clear(result.data.yk);
        clear(result.data.yt);
        for (auto& p : result.data.yk) p.related_yx_id = 0;
        for (auto& p : result.data.yt) p.related_yc_id = 0;
        return false;
    }
}

}  // namespace ems::db

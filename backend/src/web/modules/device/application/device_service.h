#pragma once
#include <common/database/mysql_database.h>

#include <functional>
#include <memory>
#include <set>

#include "web/modules/device/api/record_codec.h"
#include "web/modules/identity/public/authorization.h"

namespace ems::web {
struct DeviceResult {
    Json data;
    Json meta = Json::object();
    int status{200};
};

struct Upload {
    std::string filename;
    std::string bytes;
    Json parsed;
};

class DeviceRequest {
   public:
    database::MySqlDatabaseContext& database;
    const Principal& principal;
    core::CallContext context;
    std::unique_ptr<database::ITransaction> transaction;
    Json station;
    bool changed{false};
    std::set<std::string> deleting_points;

    /**
     * @brief 建立调用方事务，先锁站根确保查询和校验不混合不同保存。
     * @param[in,out] database 进程公共数据库上下文。
     * @param[in] principal 已认证主体。
     * @param[in] context 请求期限。
     * @param[in] write 是否使用工程写锁。
     */
    DeviceRequest(database::MySqlDatabaseContext& database, const Principal& principal,
                  core::CallContext context, bool write);
    /**
     * @brief 以绑定条件查询一页内部行，保留服务所需数字主键。
     * @param[in] query 受支持查询。
     * @return items 与 total。
     */
    Json query(database::DeviceQuery query);
    /** @brief 返回拥有自身生命周期的行数组。
     * @param[in] query 固定表查询。
     * @return 行数组，适用于范围遍历。
     */
    Json rows(database::DeviceQuery query);
    /**
     * @brief 读取指定键；需要时锁定导入任务。
     * @param[in] table 固定表。
     * @param[in] field 白名单键。
     * @param[in] value 键值。
     * @param[in] lock 是否加写锁。
     * @return 行；缺失 404。
     */
    Json one(database::DeviceTable table, const std::string& field, const std::string& value,
             bool lock = false);
    /**
     * @brief 暂存内部行，自动维护 UTC 时间字段。
     * @param[in] table 固定表。
     * @param[in] row 内部行。
     * @param[in] engineering 是否属于工程变更。
     */
    void put(database::DeviceTable table, Json row, bool engineering = true);
    /**
     * @brief 按数据库父子关系校验设备可见性。
     * @param[in] id 稳定设备 ID。
     * @return 已授权节点；越权不暴露对象存在性。
     */
    Json device(const std::string& id);
    /** @brief 判断节点是否可作为授权子树的导航节点展示，不授予写权限。
     * @param[in] id 候选节点 ID。
     * @return 属于授权子树或其祖先时为 true。
     */
    bool navigation_visible(const std::string& id);
    /**
     * @brief 检查表单计数，不自动刷新过期表单。
     * @param[in] body 含 expected_config_revision 的请求。
     */
    void expect_revision(const Json& body);
    /**
     * @brief 对当前候选工程执行跨表规则校验。
     * @param[in] device_id 检查对象，空为整个工程。
     * @return 结构化问题数组。
     */
    Json validate(const std::string& device_id = {});
    /**
     * @brief 提交；工程实际变化时一次递增站根计数。
     * @param[in] result HTTP 业务结果。
     * @return 附提交计数的结果；失败保留公共 effect。
     */
    DeviceResult finish(DeviceResult result);
};

class DeviceService {
   public:
    /**
     * @brief 注入本进程公共数据库上下文与受限工作区。
     * @param[in] database 公共上下文。
     * @param[in] workspace 导入临时目录。
     */
    DeviceService(database::MySqlDatabaseContext database, std::string workspace);
    /**
     * @brief 执行已注册设备用例；所有 JSON 使用 nlohmann。
     * @param[in] path 固定接口路径。
     * @param[in] body JSON 业务参数。
     * @param[in] principal 已认证主体。
     * @param[in] context 请求期限。
     * @param[in] upload 导入文件，仅 create-import 使用。
     * @return 业务结果或 ApiError。
     */
    DeviceResult execute(const std::string& path, const Json& body, const Principal& principal,
                         const core::CallContext& context, const Upload& upload = {});
    /**
     * @brief 验证全部 14 表可读且唯一站根存在，不建表。
     * @param[in] context 启动期限。
     */
    void check_schema(const core::CallContext& context);

   private:
    database::MySqlDatabaseContext database_;
    std::string workspace_;
};

/** @brief 处理树、档案及配置检查。
 * @param[in,out] request 活动事务。
 * @param[in] path 固定路由。
 * @param[in] body JSON。
 * @return 业务结果。
 */
DeviceResult device_action(DeviceRequest& request, const std::string& path, const Json& body);
/** @brief 处理通道和绑定。
 * @param[in,out] request 活动事务。
 * @param[in] path 固定路由。
 * @param[in] body JSON。
 * @return 业务结果。
 */
DeviceResult binding_action(DeviceRequest& request, const std::string& path, const Json& body);
/** @brief 处理四遥与固有点。
 * @param[in,out] request 活动事务。
 * @param[in] path 固定路由。
 * @param[in] body JSON。
 * @return 业务结果。
 */
DeviceResult point_action(DeviceRequest& request, const std::string& path, const Json& body);
/** @brief 处理类型、模板和能力目录。
 * @param[in,out] request 活动事务。
 * @param[in] path 固定路由。
 * @param[in] body JSON。
 * @return 业务结果。
 */
DeviceResult catalog_action(DeviceRequest& request, const std::string& path, const Json& body);
/** @brief 处理持久导入任务。
 * @param[in,out] request 活动事务。
 * @param[in] path 固定路由。
 * @param[in] body JSON metadata。
 * @param[in] upload 原文件。
 * @param[in] workspace 受限工作区。
 * @return 业务结果。
 */
DeviceResult import_action(DeviceRequest& request, const std::string& path, const Json& body,
                           const Upload& upload, const std::string& workspace);
/** @brief 生成对外 DTO，隐藏数据库主键并转换 JSON 列与大整数。
 * @param[in] row 内部行。
 * @return 对外对象。
 */
Json public_row(Json row);
/** @brief 比较创建重放内容，忽略数据库身份和服务端维护时间。
 * @param[in] existing 当前内部行。
 * @param[in] candidate 归一化创建行。
 * @return 可安全返回精确重放结果时为 true。
 */
bool same_record(Json existing, Json candidate);
/** @brief 构建分页白名单查询。
 * @param[in] table 固定表。
 * @param[in] body 分页和筛选参数。
 * @param[in] allowed 可筛选列。
 * @param[in] maximum 最大单页行数，树节点可使用 500。
 * @return 数据库查询。
 */
database::DeviceQuery page_query(database::DeviceTable table, const Json& body,
                                 std::initializer_list<const char*> allowed,
                                 std::size_t maximum = 200);
/** @brief 合并写字段，检查字段白名单及原始 JSON 类型。
 * @param[in] current 当前内部行。
 * @param[in] patch 可写字段。
 * @param[in] allowed 可写白名单。
 * @return 合并后的行。
 */
Json merge_fields(Json current, const Json& patch, std::initializer_list<const char*> allowed);
/** @brief 将四遥文本转换为固定表枚举。
 * @param[in] kind yc/yx/yk/yt。
 * @return 表；其他值抛 422。
 */
database::DeviceTable point_table(const std::string& kind);
}  // namespace ems::web

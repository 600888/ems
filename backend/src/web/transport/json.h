#pragma once
#include <common/core/result.h>

#include <initializer_list>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace ems::web {
using Json = nlohmann::json;

struct ApiError : std::runtime_error {
    int status;
    std::string code, reason, effect;
    Json details;
    /**
     * @brief 构造稳定 HTTP 业务错误，不包含底层诊断。
     * @param[in] status HTTP 状态。
     * @param[in] code 公共错误分类。
     * @param[in] reason 稳定业务原因。
     * @param[in] effect 失败写入确定性。
     * @param[in] details 脱敏字段详情。
     */
    ApiError(int status, std::string code, std::string reason, std::string effect = "NotApplied",
             Json details = Json::array());
};

/** @brief 校验对象只包含允许字段。
 * @param[in] j JSON 对象。
 * @param[in] allowed 字段白名单。
 */
void fields(const Json& j, std::initializer_list<const char*> allowed);
/** @brief 读取非空且有界字符串。
 * @param[in] j 对象。
 * @param[in] key 字段。
 * @param[in] max 最大 UTF-8 字节数。
 * @return 字符串；缺失或类型错误抛 ApiError。
 */
std::string text(const Json& j, const char* key, std::size_t max = 64);
/** @brief 将公共错误转换为 Web 错误并抛出。
 * @param[in] error 公共层错误。
 */
[[noreturn]] void fail(const core::Error& error);

/** @brief 获取公共结果，失败抛 Web 错误。
 * @param[in] result 公共结果，所有权转移给调用者。
 * @return 成功值。
 */
template <class T>
T take(core::Result<T> result) {
    if (!core::is_ok(result)) fail(core::error_of(result));
    return std::move(std::get<0>(result));
}

/** @brief 返回 UTC RFC 3339 文本。
 * @return 当前 UTC 时间。
 */
std::string utc_now();
}  // namespace ems::web

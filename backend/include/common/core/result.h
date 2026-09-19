/**
 * @file result.h
 * @brief 公共结果类型：Error/Result/WriteEffect（docs/common/design.md 第 5.1 节）
 *
 * 成功只通过返回值/Receipt 表达；WriteEffect 是失败时的写入确定性：
 *   - NotApplicable：读取类失败，不涉及任何写入；
 *   - NotApplied    ：能证明没有修改；
 *   - Unknown       ：可能已提交但未收到响应（不得当作可无条件重试）。
 *
 * 不得把"网络超时"统一映射为"可以重试"；message 只作脱敏诊断，
 * 不作为业务分支依据；公共层不返回 HTTP 状态码。
 */
#pragma once

#include <chrono>
#include <string>
#include <utility>
#include <variant>

namespace ems::core {

/// 稳定错误码（跨模块/跨语言契约的一部分，只增不改义）
enum class ErrorCode {
    InvalidArgument,     ///< 参数非法
    NotFound,            ///< 对象不存在
    Conflict,            ///< 并发冲突/唯一约束冲突/版本不符
    PermissionDenied,    ///< 权限不足
    NotReady,            ///< 对象存在但尚未就绪
    Unavailable,         ///< 依赖后端暂不可用（可退避）
    DeadlineExceeded,    ///< 调用预算耗尽
    ResourceExhausted,   ///< 池/队列/磁盘等资源预算耗尽
    Unsupported,         ///< 不支持的后端/版本/操作
    SchemaMismatch,      ///< 数据与 schema 不符（旧记录带无效 revision 等）
    CorruptData,         ///< 数据损坏（校验失败/帧损坏）
    Internal             ///< 内部错误
};

/// 失败时的写入确定性
enum class WriteEffect {
    NotApplicable,  ///< 不适用（如读取失败）
    NotApplied,     ///< 已确认未修改
    Unknown         ///< 结果未知：可能已提交但响应丢失
};

struct Error {
    ErrorCode code{ErrorCode::Internal};
    WriteEffect effect{WriteEffect::NotApplicable};
    std::string message;  ///< 脱敏诊断信息，不作为业务分支依据
};

/// Result<T>：成功携带 T，失败携带 Error（避免依赖 C++23 std::expected）
template <class T>
using Result = std::variant<T, Error>;

struct Unit {};  ///< 无载荷成功值

using Status = Result<Unit>;

/// steady_clock 期限（进程内超时预算；序列化只传业务时间或剩余预算）
using Deadline = std::chrono::steady_clock::time_point;

inline const char* to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::InvalidArgument:    return "InvalidArgument";
        case ErrorCode::NotFound:           return "NotFound";
        case ErrorCode::Conflict:           return "Conflict";
        case ErrorCode::PermissionDenied:   return "PermissionDenied";
        case ErrorCode::NotReady:           return "NotReady";
        case ErrorCode::Unavailable:        return "Unavailable";
        case ErrorCode::DeadlineExceeded:   return "DeadlineExceeded";
        case ErrorCode::ResourceExhausted:  return "ResourceExhausted";
        case ErrorCode::Unsupported:        return "Unsupported";
        case ErrorCode::SchemaMismatch:     return "SchemaMismatch";
        case ErrorCode::CorruptData:        return "CorruptData";
        case ErrorCode::Internal:           return "Internal";
    }
    return "Unknown";
}

inline const char* to_string(WriteEffect effect) noexcept {
    switch (effect) {
        case WriteEffect::NotApplicable: return "NotApplicable";
        case WriteEffect::NotApplied:    return "NotApplied";
        case WriteEffect::Unknown:       return "Unknown";
    }
    return "Unknown";
}

/// 构造 Error
inline Error make_error(ErrorCode code, WriteEffect effect, std::string message = {}) {
    Error e;
    e.code = code;
    e.effect = effect;
    e.message = std::move(message);
    return e;
}

/// 是否成功（index 0 = T）
template <class T>
bool is_ok(const Result<T>& r) noexcept {
    return r.index() == 0;
}

/// 取失败 Error（前置条件：is_ok(r) == false）
template <class T>
const Error& error_of(const Result<T>& r) noexcept {
    return std::get<1>(r);
}

/// 取成功值（前置条件：is_ok(r) == true）
template <class T>
const T& value_of(const Result<T>& r) noexcept {
    return std::get<0>(r);
}

/// 无载荷成功值
inline Status ok_status() { return Status{Unit{}}; }

}  // namespace ems::core

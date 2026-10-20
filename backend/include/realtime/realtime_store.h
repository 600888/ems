/**
 * @file realtime_store.h
 * @brief 公共实时数据访问抽象接口（website/docs/common/realtime/access-design.md 第 4/5 节）
 *
 * 两层接口中的抽象存储层：面向逻辑记录，提供单记录完整读取、条件
 * 替换（CAS）、条件删除与逐项批量结果。SharedMemoryStore、RedisStore
 * 及新增后端均实现这一契约；业务门面 RealtimeService 在其上提供
 * 类型化访问、来源代次与新鲜度检查。
 *
 * 统一保证（第 4.1 节）：
 * - 单记录读返回同一次已提交版本的完整字段副本；
 * - compareExchange 完整替换业务字段（新记录没有的旧字段必须删除）；
 * - 并发 CAS 至多一个成功；批量逐项对应，不保证整批原子性；
 * - 失败明确区分：未应用 / 结果未知 / 版本冲突 / 无数据 / 后端不可用。
 *
 * 公开头文件不得包含 hiredis、redis++、Windows API 或 pthread 类型。
 */
#pragma once

#include <common/core/call_context.h>
#include <common/core/result.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ems::realtime {

/** @brief 复用公共模块的结果类型，保证各后端错误可以互换。 */
using ErrorCode = ems::core::ErrorCode;
using WriteEffect = ems::core::WriteEffect;
using Error = ems::core::Error;
template <class T>
using Result = ems::core::Result<T>;
using Deadline = ems::core::Deadline;
using CallContext = ems::core::CallContext;

/// 作用域：单元或站（键设计专题的作用域规则）
enum class ScopeKind { Unit, Station };

struct Scope {
    ScopeKind kind{ScopeKind::Unit};
    std::string code;
};

/// 记录种类：测点 / 资产状态 / 通道状态 / 命令状态投影
enum class RecordKind { Point, AssetState, ChannelState, CommandState };

/// 逻辑身份：作用域 + 种类 + 设备路径 + 资源 code（不传 Redis 原始键/共享段 offset）
struct RecordKey {
    Scope scope;
    RecordKind kind{RecordKind::Point};
    std::vector<std::string> device_path;  ///< 递归资产路径；根为空
    std::string name;                      ///< point/channel/command code；AssetState 为空
};

/// 存储记录版本（CAS 不透明令牌；只能判等，不得当序号排序）
struct Revision {
    std::string opaque;  ///< 上限 128 字节；删除重建/共享段重建不得复用

    /**
     * @brief 判断版本令牌是否为空。
     * @return true 表示对象为空；false 表示非空。
     */
    bool empty() const noexcept { return opaque.empty(); }

    /**
     * @brief 比较两个不透明版本令牌是否相同。
     * @param[in] other 用于比较的另一个对象。
     * @return true 表示两者相等；false 表示不相等。
     */
    bool operator==(const Revision& other) const noexcept { return opaque == other.opaque; }

    /**
     * @brief 比较两个不透明版本令牌是否不同。
     * @param[in] other 用于比较的另一个对象。
     * @return true 表示两者不相等；false 表示相等。
     */
    bool operator!=(const Revision& other) const noexcept { return opaque != other.opaque; }
};

/// 规范化字段（UTF-8 文本，可含 JSON）；`_store_` 前缀保留给适配器
using Fields = std::map<std::string, std::string>;

/// 完整记录：字段副本 + 读取时的 revision
struct Record {
    Fields fields;
    Revision revision;
};

/// 条件写入请求：expected 为 nullopt 时仅在不存在时创建
struct WriteRequest {
    RecordKey key;
    Fields replacement;                ///< 完整替换，不是合并
    std::optional<Revision> expected;  ///< 版本不符返回 Conflict/NotApplied
};

/// 条件删除请求：删除也必须检查版本
struct EraseRequest {
    RecordKey key;
    Revision expected;
};

struct WriteReceipt {
    Revision revision;  ///< 写入后的新版本令牌
};

struct EraseReceipt {};

/// 后端能力信息（info() 不发网络探测）
struct BackendInfo {
    std::string implementation;
    std::string storage_generation;  ///< 诊断身份，不替代业务 source_epoch
    bool cross_process{false};
    bool cross_host{false};
    bool change_hints{false};
    std::size_t max_record_bytes{0};
    std::size_t max_batch_items{0};
};

/**
 * @brief 抽象实时存储契约
 *
 * 生命周期由进程组装者管理：停止入口 → 等待有界在途调用结束 →
 * close() → 析构；调用者不得并发 close 与访问。close 幂等，关闭后
 * 的访问返回 NotReady。实例的业务方法可并发调用。
 */
class IRealtimeStore {
   public:
    /** @brief 销毁存储实例。调用者须先停止并等待在途调用。 */
    virtual ~IRealtimeStore() = default;

    /**
     * @brief 返回后端能力信息，不执行外部探测。
     * @return 得到的后端信息。
     */
    virtual BackendInfo info() const = 0;

    /**
     * @brief 读取同一已提交版本的完整记录。
     * @param[in] key 逻辑记录键。
     * @param[in] ctx 调用预算。
     * @return 不存在时返回 NotFound，不等于记录值为零。
     */
    virtual Result<Record> read(const RecordKey& key, const CallContext& ctx) = 0;

    /**
     * @brief 按版本条件完整替换或创建记录。
     * @param[in] request 包含记录键、新字段及预期版本。
     * @param[in] ctx 调用预算。
     * @return 版本不符返回 Conflict；结果不确定时标记 Unknown。
     */
    virtual Result<WriteReceipt> compareExchange(const WriteRequest& request,
                                                 const CallContext& ctx) = 0;

    /**
     * @brief 按预期版本删除记录。
     * @return 不存在时为 NotFound/NotApplied，版本不符时为 Conflict/NotApplied。
     * @param[in] request 操作请求。
     * @param[in] ctx 调用上下文，包含截止时间等信息。
     */
    virtual Result<EraseReceipt> erase(const EraseRequest& request, const CallContext& ctx) = 0;

    /**
     * @brief 逐项批量读取；结果与输入一一对应，同键按输入顺序执行。
     * @param[in] keys 键集合。
     * @param[in] ctx 调用上下文，包含截止时间等信息。
     * @return 成功时包含记录；失败时包含错误信息。
     */
    virtual std::vector<Result<Record>> readMany(const std::vector<RecordKey>& keys,
                                                 const CallContext& ctx) = 0;

    /**
     * @brief 逐项批量写入；分别返回每项的成功或失败结果。
     * @param[in] requests 操作请求列表。
     * @param[in] ctx 调用上下文，包含截止时间等信息。
     * @return 成功时包含写入凭据；失败时包含错误信息。
     */
    virtual std::vector<Result<WriteReceipt>> writeMany(const std::vector<WriteRequest>& requests,
                                                        const CallContext& ctx) = 0;

    /** @brief 幂等关闭，停止接受新调用并结束阻塞操作。 */
    virtual void close() noexcept = 0;
};

/**
 * @brief 批量默认实现：复用逐项调度，不保存全局连接、不承载业务规则
 *
 * 同一 deadline 传入所有单项；到期后后续调用立即返回。超批量上限时
 * 每项返回 InvalidArgument 且不执行任何存储请求。优化后端可覆盖批量
 * 方法（如 pipeline），但不得改变批量语义。
 */
class RealtimeStoreBase : public IRealtimeStore {
   public:
    /**
     * @copydoc IRealtimeStore::readMany
     * @param[in] keys 键集合。
     * @param[in] ctx 调用上下文，包含截止时间等信息。
     * @return 成功时包含记录；失败时包含错误信息。
     */
    std::vector<Result<Record>> readMany(const std::vector<RecordKey>& keys,
                                         const CallContext& ctx) override {
        if (keys.size() > info().max_batch_items) {
            return std::vector<Result<Record>>(
                keys.size(), Error{ErrorCode::InvalidArgument, WriteEffect::NotApplicable,
                                   "batch limit exceeded"});
        }
        std::vector<Result<Record>> out;
        out.reserve(keys.size());
        for (const auto& key : keys) {
            out.push_back(read(key, ctx));
        }
        return out;
    }

    /**
     * @copydoc IRealtimeStore::writeMany
     * @param[in] requests 操作请求列表。
     * @param[in] ctx 调用上下文，包含截止时间等信息。
     * @return 成功时包含写入凭据；失败时包含错误信息。
     */
    std::vector<Result<WriteReceipt>> writeMany(const std::vector<WriteRequest>& requests,
                                                const CallContext& ctx) override {
        if (requests.size() > info().max_batch_items) {
            return std::vector<Result<WriteReceipt>>(
                requests.size(),
                Error{ErrorCode::InvalidArgument, WriteEffect::NotApplied, "batch limit exceeded"});
        }
        std::vector<Result<WriteReceipt>> out;
        out.reserve(requests.size());
        for (const auto& request : requests) {
            out.push_back(compareExchange(request, ctx));
        }
        return out;
    }
};

/**
 * @brief 验证逻辑记录键的组合是否合法。
 * @return 合法时返回空字符串，否则返回原因文本。
 * @param[in] key Redis 键。
 */
std::string validate_record_key(const RecordKey& key);

}  // namespace ems::realtime

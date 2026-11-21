#pragma once

#include <rtdb/client/realtime_client.h>

#include <memory>

namespace ems::rtdb {

/** @brief 不可变命令目标；目标值与设备实际反馈分开保存。 */
struct CommandRequest {
    std::string command_id;
    std::string point_id;
    double value{0};
    std::int64_t config_revision{0};
    std::int64_t expires_at_ms{0};
};

/** @brief 实时库中的独立命令记录；revision 仅用于原子认领和状态更新。 */
struct CommandRecord {
    CommandRequest request;
    std::string status{"Pending"};
    std::string reason;
    std::int64_t sent_at_ms{0};
    std::int64_t updated_at_ms{0};
    bool verify_requested{false};
    Revision revision;
};

struct CommandSubmission {
    CommandRecord record;
    bool duplicate{false};
    bool notification_queued{false};  ///< 提示失败不撤销已写入命令，消费者扫描补偿。
};

/**
 * @brief 统一命令入口：先写实时库，再发提示；查询和执行结果均读写同一记录。
 *
 * 每个 command_id 独立保存，不覆盖测点实测值。消费者必须补偿扫描，不能依赖提示可靠送达。
 * 关闭前调用方须停止并等待全部在途操作；普通业务方法可并发调用。
 */
class EMS_RTDB_API CommandClient final {
   public:
    /**
     * @brief 挂接实时库并装配异步通知发布器，不向采集侧发送命令请求。
     * @param[in] config_path 实时库部署配置路径；共享内存须由初始化进程预先创建。
     * @param[in] scope 命令作用域，须与采集执行者一致。
     * @param[in] notification_endpoint 通知订阅端点；空串仅使用补偿扫描。
     * @param[in] ctx 打开预算。
     * @return 共享拥有的客户端或明确错误；不创建或切换后端。
     */
    static Result<std::shared_ptr<CommandClient>> open(const std::string& config_path, Scope scope,
                                                       const std::string& notification_endpoint,
                                                       const CallContext& ctx);

    /** @brief 停止通知线程并释放本进程的实时库映射。 */
    ~CommandClient();

    /**
     * @brief 原子创建 Pending 命令后发布通知；同 ID 同载荷返回已有状态，不重新执行。
     * @param[in] request 稳定命令 ID、目标、配置修订号和 UTC 毫秒期限。
     * @param[in] ctx 实时库写入预算。
     * @return 已写入记录及重复标记；异载荷返回 Conflict，写入未知须用原 ID 查询。
     */
    Result<CommandSubmission> submitCommand(const CommandRequest& request, const CallContext& ctx);

    /**
     * @brief 从实时库读取命令目标、状态与执行结果。
     * @param[in] command_id 稳定命令 ID。
     * @param[in] ctx 读取预算。
     * @return 完整记录或明确错误；不存在不表示设备未执行。
     */
    Result<CommandRecord> readCommand(const std::string& command_id, const CallContext& ctx);

    /**
     * @brief 枚举命令 ID 供采集侧补偿扫描，最多 65536 条。
     * @param[in] ctx 扫描预算。
     * @return 命令 ID 集合或明确错误，不保证并发快照。
     */
    Result<std::vector<std::string>> listCommands(const CallContext& ctx);

    /**
     * @brief 执行者按 revision 原子认领或更新状态；不可修改原始命令目标。
     * @param[in] record 完整状态；revision 为空时仅创建，用于持久记录恢复。
     * @param[in] ctx 写入预算。
     * @return 带新 revision 的记录或冲突/写入错误；冲突时不得执行设备动作。
     */
    Result<CommandRecord> updateCommand(const CommandRecord& record, const CallContext& ctx);

    /**
     * @brief 为 Unknown/Acked 命令登记只读核验请求并提示消费者，不重发设备写入。
     * @param[in] command_id 待核验的命令 ID。
     * @param[in] ctx 读写预算。
     * @return 更新后的命令记录或明确错误。
     */
    Result<CommandRecord> requestVerification(const std::string& command_id,
                                              const CallContext& ctx);

   private:
    struct Impl;
    /**
     * @brief 接管客户端和异步通知实现。
     * @param[in] impl 实现对象，所有权转交本客户端。
     */
    explicit CommandClient(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace ems::rtdb

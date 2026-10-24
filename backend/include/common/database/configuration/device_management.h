/**
 * @file device_management.h
 * @brief 设备管理表行结构（14 张表），与 003 迁移逐列对应。
 *
 * 设计见 website/docs/common/database/device-management.md：
 *   · 只有设备：cfg_device 一棵树，node_kind 区分站/单元/设备/部件
 *   · 四遥四表：cfg_point_yc / _yx / _yk / _yt
 *   · 不做配置版本：表可原地改，热加载靠站根 config_revision
 *
 * 行结构约定：
 *   - 字段顺序与 DDL 列顺序一致（ormpp 的 SELECT * 按位置映射）。
 *   - SQL NULL 用 0 / 空串表达（ormpp 只对算术类型清零），语义见各字段注释。
 *   - JSON 列在行里是 JSON 文本，由服务解析；空值为 "[]" 或 "null"。
 *   - 数字自增主键 id 仅供连接，不对外；稳定业务 ID 是文本列。
 */
#pragma once

#include <cstdint>
#include <string>

namespace ems::database {

/// cfg_device：设备树节点（站/单元/设备/部件）
struct CfgDeviceRow {
    std::int64_t id{0};
    std::string device_id;         ///< 稳定设备 ID，全树唯一
    std::string parent_device_id;  ///< 父节点；站根为空串
    std::string node_kind;         ///< station/unit/device/component
    std::string code;              ///< 同父节点下唯一
    std::string name;
    std::string path;  ///< 相对站根的路径，如 /unit01/battery01
    int depth{0};      ///< 0–8
    int sort_order{0};
    std::string device_type_code;  ///< 部件节点同样引用类型（电池簇/包/电芯）
    std::string type_version;
    std::string template_id;       ///< 创建时套用的型号模板，仅记录来源
    std::string source_device_id;  ///< component 必填：采集它的通信设备
    std::string vendor;
    std::string model;
    std::string firmware_version;
    double rated_power_kw{0.0};
    std::string timezone;               ///< 仅站根使用
    int default_stale_after_ms{60000};  ///< 仅站根使用；0 表示不判陈旧
    std::int64_t config_revision{0};    ///< 仅站根使用：每次保存 +1，供热加载轮询
    int enabled{1};                     ///< 配置启用，与运行在线状态无关
    std::string description;
    std::string legacy_ref;  ///< 旧四遥表数字 ID
    std::string created_at;
    std::string updated_at;
};

/// cfg_device_type：设备类型（含类型版本）
struct CfgDeviceTypeRow {
    std::string type_code;
    std::string type_version;
    std::string name;
    std::string category;  ///< pcs/bms/hvac/env/fire/meter/relay/battery/rack/pack/cell
    std::string status;    ///< active/retired
    std::string description;
    std::string created_at;
    std::string updated_at;
};

/// cfg_type_point：类型固有点目录（四遥）
struct CfgTypePointRow {
    std::string type_code;
    std::string type_version;
    std::string semantic_code;  ///< 如 active_power、run_state、start
    std::string name;
    std::string point_type;  ///< yc/yx/yk/yt，决定映射行落在哪张点表
    std::string value_type;
    std::string unit;
    int required{1};                    ///< 属于任一能力即为必需
    std::string capability_codes_json;  ///< 所属能力数组，可属多个能力
    int writable{0};
    int stale_after_ms{0};  ///< 0 表示继承站默认
    int sort_order{0};
};

/// cfg_template：型号模板（点表为 JSON 复制源，不参与运行期联表）
struct CfgTemplateRow {
    std::string template_id;       ///< 如 PCS-125K
    std::string template_version;  ///< 如 v2.3
    std::string device_type_code;
    std::string type_version;
    std::string vendor;
    std::string model;
    std::string status;       ///< active/retired
    std::string points_json;  ///< 默认四遥点表与映射
    std::string description;
    std::string created_at;
    std::string updated_at;
};

/// cfg_channel：通道（一条通道即一条物理连接）
struct CfgChannelRow {
    std::int64_t id{0};
    std::string channel_id;
    std::string code;
    std::string name;
    std::string protocol;   ///< modbus/iec104/dlt645/iec61850
    std::string conn_mode;  ///< tcp_client/tcp_server/serial/udp
    std::string endpoint;   ///< host:port 或 COM3
    std::string serial_json;
    int poll_interval_ms{1000};
    int timeout_ms{3000};
    int retry_times{1};
    int max_sessions{1};  ///< 允许并发的绑定数，表达共享连接
    int enabled{1};
    std::string description;
    std::string created_at;
    std::string updated_at;
};

/// cfg_binding：设备-通道绑定与角色标志
struct CfgBindingRow {
    std::int64_t id{0};
    std::string binding_id;
    std::string device_id;
    std::string channel_id;
    std::string device_address;   ///< Unit ID / 公共地址 / 从站号
    std::string profile_version;  ///< 如 mbtcp-v3
    int can_collect{0};
    int can_control{0};
    int can_forward{0};
    int enabled{1};
    std::string description;
    std::string created_at;
    std::string updated_at;
};

/// cfg_point_yc：遥测（倍率、量程、死区）
struct CfgPointYcRow {
    std::int64_t id{0};
    std::string point_id;         ///< 稳定点 ID，四表之间也全局唯一（服务校验）
    std::string owner_device_id;  ///< 点归属节点；部件点填 rack/pack/cell
    std::string binding_id;       ///< 实际采集来源绑定；部件点填 BMS 的绑定
    std::string code;
    std::string name;
    std::string semantic_code;  ///< 非空=固有点映射；空=厂家扩展点
    std::string vendor_key;     ///< 厂家点稳定键，重复导入据此更新
    std::string origin;         ///< type/template/vendor/component
    std::string address_space;  ///< 空=非寄存器协议
    int address_offset{0};
    int bit_offset{0};
    int register_count{1};
    std::string protocol_address_json;
    std::string wire_type;
    std::string byte_order;  ///< ab/ba；空=单寄存器
    std::string value_type;  ///< double/int64/uint64
    std::string unit;
    int decimals{0};  ///< 0 表示按值类型默认
    double scale_factor{1.0};
    double offset_value{0.0};
    double range_min{-9999999.0};
    double range_max{9999999.0};
    double deadband{0.0};  ///< 变化死区，0 表示逐点上报
    int enabled{1};
    std::string disabled_reason;
    int stale_after_ms{0};
    std::string source_import_row_id;
    std::string legacy_ref;
    std::string description;
    std::string created_at;
    std::string updated_at;
};

/// cfg_point_yx：遥信（变位/SOE、取反、码表）
struct CfgPointYxRow {
    std::int64_t id{0};
    std::string point_id;
    std::string owner_device_id;
    std::string binding_id;
    std::string code;
    std::string name;
    std::string semantic_code;
    std::string vendor_key;
    std::string origin;
    std::string address_space;
    int address_offset{0};
    int bit_offset{0};
    int register_count{1};
    std::string protocol_address_json;
    std::string wire_type;
    std::string byte_order;
    std::string value_type;  ///< bool/int64/string
    std::string unit;
    int soe{0};     ///< 记录变位；事件不是第五类遥，仍属遥信
    int invert{0};  ///< 原始位取反（常闭接点）
    std::string enum_json;
    std::string unknown_policy;        ///< retain_raw/uncertain/invalid
    std::string feedback_of_point_id;  ///< 反馈点指回遥控/遥调点
    int enabled{1};
    std::string disabled_reason;
    int stale_after_ms{0};
    std::string source_import_row_id;
    std::string legacy_ref;
    std::string description;
    std::string created_at;
    std::string updated_at;
};

/// cfg_point_yk：遥控（固定值、脉冲、写组、反馈判据）
struct CfgPointYkRow {
    std::int64_t id{0};
    std::string point_id;
    std::string owner_device_id;
    std::string binding_id;
    std::string code;
    std::string name;
    std::string semantic_code;
    std::string vendor_key;
    std::string origin;
    std::string address_space;
    int address_offset{0};
    int bit_offset{0};
    int register_count{1};
    std::string protocol_address_json;
    std::string wire_type;
    std::string byte_order;
    std::string value_type;  ///< bool/int64
    std::string unit;
    std::string write_mode;           ///< fixed/direct
    std::int64_t fixed_raw_value{0};  ///< fixed 模式必写
    int pulse_width_ms{0};            ///< 0 表示保持
    int min_interval_ms{0};
    std::string write_group_key;  ///< 同组写寄存器互斥
    std::string feedback_json;
    int feedback_timeout_ms{5000};
    std::string failure_policy;  ///< reject/retry/escalate
    int enabled{1};
    std::string disabled_reason;
    int stale_after_ms{0};
    std::string source_import_row_id;
    std::string legacy_ref;
    std::string description;
    std::string created_at;
    std::string updated_at;
};

/// cfg_point_yt：遥调（工程值换算、可写量程、反馈判据）
struct CfgPointYtRow {
    std::int64_t id{0};
    std::string point_id;
    std::string owner_device_id;
    std::string binding_id;
    std::string code;
    std::string name;
    std::string semantic_code;
    std::string vendor_key;
    std::string origin;
    std::string address_space;
    int address_offset{0};
    int bit_offset{0};
    int register_count{1};
    std::string protocol_address_json;
    std::string wire_type;
    std::string byte_order;
    std::string value_type;  ///< double/int64
    std::string unit;
    int decimals{0};
    double scale_factor{1.0};  ///< 原码 = (工程值 - offset) / scale
    double offset_value{0.0};
    double range_min{-9999999.0};
    double range_max{9999999.0};
    int min_interval_ms{0};
    std::string write_group_key;
    std::string feedback_json;
    int feedback_timeout_ms{5000};
    std::string failure_policy;
    int enabled{1};
    std::string disabled_reason;
    int stale_after_ms{0};
    std::string source_import_row_id;
    std::string legacy_ref;
    std::string description;
    std::string created_at;
    std::string updated_at;
};

/// cfg_import_job：点表导入任务与幂等键
struct CfgImportJobRow {
    std::string import_job_id;
    std::string device_id;
    std::string binding_id;
    std::string source_kind;  ///< vendor_csv/vendor_xlsx/telemetry_xlsx
    std::string file_name;
    std::string file_sha256;
    int file_bytes{0};
    std::string address_base;  ///< zero/one
    std::string status;        ///< parsed/confirmed/applied/failed
    int row_total{0};
    int row_ok{0};
    int row_warning{0};
    int row_error{0};
    std::string idempotency_key;  ///< SHA-256(device|binding|sha256)
    std::string submitted_by;
    std::string submitted_at;
    std::string finished_at;
    std::string error_code;
    std::string error_message;
};

/// cfg_import_row：导入逐行解析与逐项确认
struct CfgImportRow {
    std::int64_t id{0};
    std::string import_job_id;
    int line_no{0};
    std::string vendor_key;
    std::string vendor_name;
    std::string point_type;          ///< yc/yx/yk/yt，决定写入哪张点表
    std::string target_device_path;  ///< 空即“目标节点缺失”
    std::string semantic_code;
    std::string address_space;
    int address_offset{0};
    int bit_offset{0};
    int register_count{1};
    std::string protocol_address_json;
    std::string wire_type;
    std::string byte_order;
    std::string value_type;
    double scale_factor{1.0};
    double offset_value{0.0};
    std::string unit;
    std::string parse_status;  ///< ok/warning/error
    std::string parse_code;
    std::string parse_message;
    std::string confirm_state;  ///< none/accepted/fixed/rejected
    std::string confirm_note;
    std::string confirm_by;
    std::string confirm_at;
    std::string resolved_point_id;
};

/// ops_comm_stat_5m：绑定维度 5 分钟通信统计
struct OpsCommStat5mRow {
    std::string binding_id;
    std::string window_start;
    std::int64_t request_total{0};
    std::int64_t success_total{0};
    std::int64_t timeout_total{0};
    std::int64_t error_total{0};
    int avg_latency_ms{0};
    int p95_latency_ms{0};
    std::string last_valid_at;
    std::string last_error_code;
};

/// ops_state_log：设备/通道/绑定状态变化历史
struct OpsStateLogRow {
    std::int64_t id{0};
    std::string object_kind;  ///< device/channel/binding
    std::string object_id;
    std::string state;  ///< unconfigured/online/alarm/fault/offline
    std::string since;
    std::string reason;
};

}  // namespace ems::database

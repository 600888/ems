/**
 * @file oi.hpp
 * @brief DL/T 698.45—2017 已收录的常用对象标识常量。
 * @note OI 仅标识对象；属性及元素索引由 OAD 指定，设备支持情况须另行确认。
 */
#pragma once
#include <cstdint>

namespace dlt698::standard::oi {
/// 组合有功电能（附录 E.1）。
inline constexpr std::uint16_t combination_active_energy = 0x0000;
/// 正向有功电能（附录 E.1）。
inline constexpr std::uint16_t forward_active_energy = 0x0010;
/// 反向有功电能（附录 E.1）。
inline constexpr std::uint16_t reverse_active_energy = 0x0020;
/// 组合无功 1 电能（附录 E.1）。
inline constexpr std::uint16_t combination_reactive_energy_1 = 0x0030;
/// 组合无功 2 电能（附录 E.1）。
inline constexpr std::uint16_t combination_reactive_energy_2 = 0x0040;
/// 电压（附录 E.3）。
inline constexpr std::uint16_t voltage = 0x2000;
/// 电流（附录 E.3）。
inline constexpr std::uint16_t current = 0x2001;
/// 有功功率（附录 E.3）。
inline constexpr std::uint16_t active_power = 0x2004;
/// 无功功率（附录 E.3）。
inline constexpr std::uint16_t reactive_power = 0x2005;
/// 视在功率（附录 E.3）。
inline constexpr std::uint16_t apparent_power = 0x2006;
/// 功率因数（附录 E.3）。
inline constexpr std::uint16_t power_factor = 0x200a;
/// 电网频率（附录 E.3）。
inline constexpr std::uint16_t frequency = 0x200f;
/// 日期时间（附录 E.5）。
inline constexpr std::uint16_t date_time = 0x4000;
/// 电能表通信地址（附录 E.5）。
inline constexpr std::uint16_t communication_address = 0x4001;
/// 正向有功最大需量（附录 E.2）。
inline constexpr std::uint16_t forward_active_maximum_demand = 0x1010;
/// A相正向有功电能（附录 E.1）。
inline constexpr std::uint16_t forward_active_energy_a = 0x0011;
/// A相正向有功最大需量（附录 E.2）。
inline constexpr std::uint16_t forward_active_maximum_demand_a = 0x1011;
/// B相正向有功电能（附录 E.1）。
inline constexpr std::uint16_t forward_active_energy_b = 0x0012;
/// B相正向有功最大需量（附录 E.2）。
inline constexpr std::uint16_t forward_active_maximum_demand_b = 0x1012;
/// C相正向有功电能（附录 E.1）。
inline constexpr std::uint16_t forward_active_energy_c = 0x0013;
/// C相正向有功最大需量（附录 E.2）。
inline constexpr std::uint16_t forward_active_maximum_demand_c = 0x1013;
/// 反向有功最大需量（附录 E.2）。
inline constexpr std::uint16_t reverse_active_maximum_demand = 0x1020;
/// A相反向有功电能（附录 E.1）。
inline constexpr std::uint16_t reverse_active_energy_a = 0x0021;
/// A相反向有功最大需量（附录 E.2）。
inline constexpr std::uint16_t reverse_active_maximum_demand_a = 0x1021;
/// B相反向有功电能（附录 E.1）。
inline constexpr std::uint16_t reverse_active_energy_b = 0x0022;
/// B相反向有功最大需量（附录 E.2）。
inline constexpr std::uint16_t reverse_active_maximum_demand_b = 0x1022;
/// C相反向有功电能（附录 E.1）。
inline constexpr std::uint16_t reverse_active_energy_c = 0x0023;
/// C相反向有功最大需量（附录 E.2）。
inline constexpr std::uint16_t reverse_active_maximum_demand_c = 0x1023;
/// 组合无功1最大需量（附录 E.2）。
inline constexpr std::uint16_t combination_reactive_1_maximum_demand = 0x1030;
/// A相组合无功1电能（附录 E.1）。
inline constexpr std::uint16_t combination_reactive_energy_1_a = 0x0031;
/// A相组合无功1最大需量（附录 E.2）。
inline constexpr std::uint16_t combination_reactive_1_maximum_demand_a = 0x1031;
/// B相组合无功1电能（附录 E.1）。
inline constexpr std::uint16_t combination_reactive_energy_1_b = 0x0032;
/// B相组合无功1最大需量（附录 E.2）。
inline constexpr std::uint16_t combination_reactive_1_maximum_demand_b = 0x1032;
/// C相组合无功1电能（附录 E.1）。
inline constexpr std::uint16_t combination_reactive_energy_1_c = 0x0033;
/// C相组合无功1最大需量（附录 E.2）。
inline constexpr std::uint16_t combination_reactive_1_maximum_demand_c = 0x1033;
/// 组合无功2最大需量（附录 E.2）。
inline constexpr std::uint16_t combination_reactive_2_maximum_demand = 0x1040;
/// A相组合无功2电能（附录 E.1）。
inline constexpr std::uint16_t combination_reactive_energy_2_a = 0x0041;
/// A相组合无功2最大需量（附录 E.2）。
inline constexpr std::uint16_t combination_reactive_2_maximum_demand_a = 0x1041;
/// B相组合无功2电能（附录 E.1）。
inline constexpr std::uint16_t combination_reactive_energy_2_b = 0x0042;
/// B相组合无功2最大需量（附录 E.2）。
inline constexpr std::uint16_t combination_reactive_2_maximum_demand_b = 0x1042;
/// C相组合无功2电能（附录 E.1）。
inline constexpr std::uint16_t combination_reactive_energy_2_c = 0x0043;
/// C相组合无功2最大需量（附录 E.2）。
inline constexpr std::uint16_t combination_reactive_2_maximum_demand_c = 0x1043;
/// 第一象限无功电能（附录 E.1）。
inline constexpr std::uint16_t quadrant_1_reactive_energy = 0x0050;
/// 第一象限最大需量（附录 E.2）。
inline constexpr std::uint16_t quadrant_1_reactive_maximum_demand = 0x1050;
/// A相第一象限无功电能（附录 E.1）。
inline constexpr std::uint16_t quadrant_1_reactive_energy_a = 0x0051;
/// A相第一象限最大需量（附录 E.2）。
inline constexpr std::uint16_t quadrant_1_reactive_maximum_demand_a = 0x1051;
/// B相第一象限无功电能（附录 E.1）。
inline constexpr std::uint16_t quadrant_1_reactive_energy_b = 0x0052;
/// B相第一象限最大需量（附录 E.2）。
inline constexpr std::uint16_t quadrant_1_reactive_maximum_demand_b = 0x1052;
/// C相第一象限无功电能（附录 E.1）。
inline constexpr std::uint16_t quadrant_1_reactive_energy_c = 0x0053;
/// C相第一象限最大需量（附录 E.2）。
inline constexpr std::uint16_t quadrant_1_reactive_maximum_demand_c = 0x1053;
/// 第二象限无功电能（附录 E.1）。
inline constexpr std::uint16_t quadrant_2_reactive_energy = 0x0060;
/// 第二象限最大需量（附录 E.2）。
inline constexpr std::uint16_t quadrant_2_reactive_maximum_demand = 0x1060;
/// A相第二象限无功电能（附录 E.1）。
inline constexpr std::uint16_t quadrant_2_reactive_energy_a = 0x0061;
/// A相第二象限最大需量（附录 E.2）。
inline constexpr std::uint16_t quadrant_2_reactive_maximum_demand_a = 0x1061;
/// B相第二象限无功电能（附录 E.1）。
inline constexpr std::uint16_t quadrant_2_reactive_energy_b = 0x0062;
/// B相第二象限最大需量（附录 E.2）。
inline constexpr std::uint16_t quadrant_2_reactive_maximum_demand_b = 0x1062;
/// C相第二象限无功电能（附录 E.1）。
inline constexpr std::uint16_t quadrant_2_reactive_energy_c = 0x0063;
/// C相第二象限最大需量（附录 E.2）。
inline constexpr std::uint16_t quadrant_2_reactive_maximum_demand_c = 0x1063;
/// 第三象限无功电能（附录 E.1）。
inline constexpr std::uint16_t quadrant_3_reactive_energy = 0x0070;
/// 第三象限最大需量（附录 E.2）。
inline constexpr std::uint16_t quadrant_3_reactive_maximum_demand = 0x1070;
/// A相第三象限无功电能（附录 E.1）。
inline constexpr std::uint16_t quadrant_3_reactive_energy_a = 0x0071;
/// A相第三象限最大需量（附录 E.2）。
inline constexpr std::uint16_t quadrant_3_reactive_maximum_demand_a = 0x1071;
/// B相第三象限无功电能（附录 E.1）。
inline constexpr std::uint16_t quadrant_3_reactive_energy_b = 0x0072;
/// B相第三象限最大需量（附录 E.2）。
inline constexpr std::uint16_t quadrant_3_reactive_maximum_demand_b = 0x1072;
/// C相第三象限无功电能（附录 E.1）。
inline constexpr std::uint16_t quadrant_3_reactive_energy_c = 0x0073;
/// C相第三象限最大需量（附录 E.2）。
inline constexpr std::uint16_t quadrant_3_reactive_maximum_demand_c = 0x1073;
/// 第四象限无功电能（附录 E.1）。
inline constexpr std::uint16_t quadrant_4_reactive_energy = 0x0080;
/// 第四象限最大需量（附录 E.2）。
inline constexpr std::uint16_t quadrant_4_reactive_maximum_demand = 0x1080;
/// A相第四象限无功电能（附录 E.1）。
inline constexpr std::uint16_t quadrant_4_reactive_energy_a = 0x0081;
/// A相第四象限最大需量（附录 E.2）。
inline constexpr std::uint16_t quadrant_4_reactive_maximum_demand_a = 0x1081;
/// B相第四象限无功电能（附录 E.1）。
inline constexpr std::uint16_t quadrant_4_reactive_energy_b = 0x0082;
/// B相第四象限最大需量（附录 E.2）。
inline constexpr std::uint16_t quadrant_4_reactive_maximum_demand_b = 0x1082;
/// C相第四象限无功电能（附录 E.1）。
inline constexpr std::uint16_t quadrant_4_reactive_energy_c = 0x0083;
/// C相第四象限最大需量（附录 E.2）。
inline constexpr std::uint16_t quadrant_4_reactive_maximum_demand_c = 0x1083;
/// 正向视在电能（附录 E.1）。
inline constexpr std::uint16_t forward_apparent_energy = 0x0090;
/// 正向视在最大需量（附录 E.2）。
inline constexpr std::uint16_t forward_apparent_maximum_demand = 0x1090;
/// A相正向视在电能（附录 E.1）。
inline constexpr std::uint16_t forward_apparent_energy_a = 0x0091;
/// A相正向视在最大需量（附录 E.2）。
inline constexpr std::uint16_t forward_apparent_maximum_demand_a = 0x1091;
/// B相正向视在电能（附录 E.1）。
inline constexpr std::uint16_t forward_apparent_energy_b = 0x0092;
/// B相正向视在最大需量（附录 E.2）。
inline constexpr std::uint16_t forward_apparent_maximum_demand_b = 0x1092;
/// C相正向视在电能（附录 E.1）。
inline constexpr std::uint16_t forward_apparent_energy_c = 0x0093;
/// C相正向视在最大需量（附录 E.2）。
inline constexpr std::uint16_t forward_apparent_maximum_demand_c = 0x1093;
/// 反向视在电能（附录 E.1）。
inline constexpr std::uint16_t reverse_apparent_energy = 0x00a0;
/// 反向视在最大需量（附录 E.2）。
inline constexpr std::uint16_t reverse_apparent_maximum_demand = 0x10a0;
/// A相反向视在电能（附录 E.1）。
inline constexpr std::uint16_t reverse_apparent_energy_a = 0x00a1;
/// A相反向视在最大需量（附录 E.2）。
inline constexpr std::uint16_t reverse_apparent_maximum_demand_a = 0x10a1;
/// B相反向视在电能（附录 E.1）。
inline constexpr std::uint16_t reverse_apparent_energy_b = 0x00a2;
/// B相反向视在最大需量（附录 E.2）。
inline constexpr std::uint16_t reverse_apparent_maximum_demand_b = 0x10a2;
/// C相反向视在电能（附录 E.1）。
inline constexpr std::uint16_t reverse_apparent_energy_c = 0x00a3;
/// C相反向视在最大需量（附录 E.2）。
inline constexpr std::uint16_t reverse_apparent_maximum_demand_c = 0x10a3;
/// 电压波形失真度（附录 E.3）。
inline constexpr std::uint16_t voltage_distortion = 0x200b;
/// 电流波形失真度（附录 E.3）。
inline constexpr std::uint16_t current_distortion = 0x200c;
/// 电压谐波含有量（附录 E.3）。
inline constexpr std::uint16_t voltage_harmonics = 0x200d;
/// 电流谐波含有量（附录 E.3）。
inline constexpr std::uint16_t current_harmonics = 0x200e;
/// 表内温度（附录 E.3）。
inline constexpr std::uint16_t internal_temperature = 0x2010;
/// 电能表运行状态字（附录 E.3）。
inline constexpr std::uint16_t operating_status = 0x2014;
/// 电能表跟随上报状态字（附录 E.3）。
inline constexpr std::uint16_t follow_report_status = 0x2015;
/// 当前有功需量（附录 E.3）。
inline constexpr std::uint16_t current_active_demand = 0x2017;
/// 当前无功需量（附录 E.3）。
inline constexpr std::uint16_t current_reactive_demand = 0x2018;
/// 当前视在需量（附录 E.3）。
inline constexpr std::uint16_t current_apparent_demand = 0x2019;
/// 表号（附录 E.5）。
inline constexpr std::uint16_t meter_number = 0x4002;
/// 客户编号（附录 E.5）。
inline constexpr std::uint16_t customer_number = 0x4003;
/// 时钟源（附录 E.5）。
inline constexpr std::uint16_t clock_source = 0x4006;
/// 时区时段数（附录 E.5）。
inline constexpr std::uint16_t time_period_counts = 0x400c;
/// 阶梯数（附录 E.5）。
inline constexpr std::uint16_t step_count = 0x400d;
/// 谐波分析次数（附录 E.5）。
inline constexpr std::uint16_t harmonic_analysis_order = 0x400e;
/// 计量元件数（附录 E.5）。
inline constexpr std::uint16_t metering_element_count = 0x4010;
/// 周休日特征字（附录 E.5）。
inline constexpr std::uint16_t weekend_characteristic = 0x4012;
/// 电压合格率参数（附录 E.5）。
inline constexpr std::uint16_t voltage_quality_limits = 0x4030;
/// 最大需量周期（附录 E.5）。
inline constexpr std::uint16_t maximum_demand_period = 0x4100;
/// 滑差时间（附录 E.5）。
inline constexpr std::uint16_t sliding_interval = 0x4101;
/// 资产管理编码（附录 E.5）。
inline constexpr std::uint16_t asset_code = 0x4103;
/// 额定电压（附录 E.5）。
inline constexpr std::uint16_t rated_voltage = 0x4104;
/// 额定电流/基本电流（附录 E.5）。
inline constexpr std::uint16_t rated_current = 0x4105;
/// 最大电流（附录 E.5）。
inline constexpr std::uint16_t maximum_current = 0x4106;
/// 有功准确度等级（附录 E.5）。
inline constexpr std::uint16_t active_accuracy_class = 0x4107;
/// 无功准确度等级（附录 E.5）。
inline constexpr std::uint16_t reactive_accuracy_class = 0x4108;
/// 电能表型号（附录 E.5）。
inline constexpr std::uint16_t meter_model = 0x410b;
/// 事件发生时间（附录 E.3）。
inline constexpr std::uint16_t event_start_time = 0x201e;
/// 事件结束时间（附录 E.3）。
inline constexpr std::uint16_t event_end_time = 0x2020;
/// 数据冻结时间（附录 E.3）。
inline constexpr std::uint16_t freeze_time = 0x2021;
/// 事件记录序号（附录 E.3）。
inline constexpr std::uint16_t event_sequence = 0x2022;
/// 冻结记录序号（附录 E.3）。
inline constexpr std::uint16_t freeze_sequence = 0x2023;
/// 事件发生源（附录 E.3）；类型由具体事件定义，不是通用 NULL 对象。
inline constexpr std::uint16_t event_source = 0x2024;
/// 电能表掉电事件（附录 E.4）。
inline constexpr std::uint16_t meter_power_down_event = 0x3011;
/// 终端初始化事件（附录 E.4）。
inline constexpr std::uint16_t terminal_initialization_event = 0x3100;
/// 日冻结（附录 E.6）。
inline constexpr std::uint16_t daily_freeze = 0x5004;
/// 月冻结（附录 E.6）。
inline constexpr std::uint16_t monthly_freeze = 0x5006;
/// ESAM 接口类；属性和操作需绑定真实提供者，目录不保存密钥。
inline constexpr std::uint16_t esam = 0xf100;
/// 显式安全模式参数；仅描述对象值，不自动更改安全后端策略。
inline constexpr std::uint16_t security_mode = 0xf101;
/// RS-232 端口列表与透明代理的端口 OAD。
inline constexpr std::uint16_t rs232 = 0xf200;
/// RS-485 端口列表与透明代理的端口 OAD。
inline constexpr std::uint16_t rs485 = 0xf201;
}  // namespace dlt698::standard::oi

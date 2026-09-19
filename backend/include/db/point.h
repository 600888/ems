/**
 * @file point.h
 * @brief 四遥测点配置表 + 采集数据 ormpp 实体定义
 */
#pragma once

#include <cstdint>
#include <string>

namespace ems {
namespace db {

struct point_yc {
    int64_t id = 0;
    std::string code;
    std::string name;
    int64_t channel_id = 0;
    int64_t device_id = 0;

    int protocol_type = 1;
    int func_code = 3;  // Modbus 功能码: 1=Coil,2=DiscreteInput,3=HoldingRegister,4=InputRegister
    int slave_id = 1;
    int reg_addr = 0;
    int bit_position = 0;
    int64_t data_id = 0;
    int casdu = 0;
    int64_t ioa = 0;
    std::string object_ref;

    int decode_code = 0;
    int endian = 0;  // 端序: 0=BIG_ENDIAN, 1=LITTLE_ENDIAN, ...
    double mul_coe = 1.0;
    double add_coe = 0.0;
    double min_limit = -9999999.0;
    double max_limit = 9999999.0;
    std::string unit;

    int enable = 1;
    std::string description;
    std::string created_at;
    std::string updated_at;
};

struct point_yx {
    int64_t id = 0;
    std::string code;
    std::string name;
    int64_t channel_id = 0;
    int64_t device_id = 0;

    int protocol_type = 1;
    int func_code = 1;  // Modbus 功能码: 遥信默认用 1=Coil
    int slave_id = 1;
    int reg_addr = 0;
    int bit_position = 0;
    int64_t data_id = 0;
    int casdu = 0;
    int64_t ioa = 0;
    std::string object_ref;

    int decode_code = 0;
    int endian = 0;
    int bit = 0;
    int reverse_flag = 0;

    int enable = 1;
    std::string description;
    std::string created_at;
    std::string updated_at;
};

struct point_yk {
    int64_t id = 0;
    std::string code;
    std::string name;
    int64_t channel_id = 0;
    int64_t device_id = 0;

    int protocol_type = 1;
    int func_code = 5;  // Modbus 功能码: 遥控默认用 5=WriteSingleCoil
    int slave_id = 1;
    int reg_addr = 0;
    int bit_position = 0;
    int64_t data_id = 0;
    int casdu = 0;
    int64_t ioa = 0;
    std::string object_ref;

    int decode_code = 0;
    int endian = 0;
    int bit = 0;
    int command_type = 0;
    int64_t related_yx_id = 0;

    int enable = 1;
    std::string description;
    std::string created_at;
    std::string updated_at;
};

struct point_yt {
    int64_t id = 0;
    std::string code;
    std::string name;
    int64_t channel_id = 0;
    int64_t device_id = 0;

    int protocol_type = 1;
    int func_code = 6;  // Modbus 功能码: 遥调默认用 6=WriteSingleRegister
    int slave_id = 1;
    int reg_addr = 0;
    int bit_position = 0;
    int64_t data_id = 0;
    int casdu = 0;
    int64_t ioa = 0;
    std::string object_ref;

    int decode_code = 0;
    int endian = 0;
    double mul_coe = 1.0;
    double add_coe = 0.0;
    double min_limit = -9999999.0;
    double max_limit = 9999999.0;
    std::string unit;

    int enable = 1;
    std::string description;
    std::string created_at;
    std::string updated_at;
};

struct point_collect {
    int64_t id = 0;
    std::string point_code;
    int point_type = 0;
    int64_t channel_id = 0;
    int64_t device_id = 0;
    int64_t raw_value = 0;
    double eng_value = 0.0;
    int quality = 0;
    std::string collect_time;
};

}  // namespace db
}  // namespace ems

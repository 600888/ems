/**
 * @file point_entity.h
 * @brief 四遥测点配置表 + 采集数据 ormpp 实体定义
 */
#pragma once

#include <string>
#include <cstdint>

namespace ems {
namespace db {

struct PointYcEntity {
    int64_t     id             = 0;
    std::string code;
    std::string name;
    int64_t     channel_id     = 0;
    int64_t     device_id      = 0;

    int         protocol_type  = 1;
    int         slave_id       = 1;
    int         reg_addr       = 0;
    int         bit_position   = 0;
    int64_t     data_id        = 0;
    int         casdu          = 0;
    int64_t     ioa            = 0;
    std::string object_ref;

    int         decode_code    = 0;
    int         endian         = 0;       // 端序: 0=BIG_ENDIAN, 1=LITTLE_ENDIAN, ...
    double      mul_coe        = 1.0;
    double      add_coe        = 0.0;
    double      min_limit      = -9999999.0;
    double      max_limit      = 9999999.0;
    std::string unit;

    int         enable         = 1;
    std::string description;
    std::string created_at;
    std::string updated_at;
};

struct PointYxEntity {
    int64_t     id             = 0;
    std::string code;
    std::string name;
    int64_t     channel_id     = 0;
    int64_t     device_id      = 0;

    int         protocol_type  = 1;
    int         slave_id       = 1;
    int         reg_addr       = 0;
    int         bit_position   = 0;
    int64_t     data_id        = 0;
    int         casdu          = 0;
    int64_t     ioa            = 0;
    std::string object_ref;

    int         decode_code    = 0;
    int         endian         = 0;
    int         bit            = 0;
    int         reverse_flag   = 0;

    int         enable         = 1;
    std::string description;
    std::string created_at;
    std::string updated_at;
};

struct PointYkEntity {
    int64_t     id             = 0;
    std::string code;
    std::string name;
    int64_t     channel_id     = 0;
    int64_t     device_id      = 0;

    int         protocol_type  = 1;
    int         slave_id       = 1;
    int         reg_addr       = 0;
    int         bit_position   = 0;
    int64_t     data_id        = 0;
    int         casdu          = 0;
    int64_t     ioa            = 0;
    std::string object_ref;

    int         decode_code    = 0;
    int         endian         = 0;
    int         bit            = 0;
    int         command_type   = 0;
    int64_t     related_yx_id  = 0;

    int         enable         = 1;
    std::string description;
    std::string created_at;
    std::string updated_at;
};

struct PointYtEntity {
    int64_t     id             = 0;
    std::string code;
    std::string name;
    int64_t     channel_id     = 0;
    int64_t     device_id      = 0;

    int         protocol_type  = 1;
    int         slave_id       = 1;
    int         reg_addr       = 0;
    int         bit_position   = 0;
    int64_t     data_id        = 0;
    int         casdu          = 0;
    int64_t     ioa            = 0;
    std::string object_ref;

    int         decode_code    = 0;
    int         endian         = 0;
    double      mul_coe        = 1.0;
    double      add_coe        = 0.0;
    double      min_limit      = -9999999.0;
    double      max_limit      = 9999999.0;
    std::string unit;

    int         enable         = 1;
    std::string description;
    std::string created_at;
    std::string updated_at;
};

struct PointCollectEntity {
    int64_t     id             = 0;
    std::string point_code;
    int         point_type     = 0;
    int64_t     channel_id     = 0;
    int64_t     device_id      = 0;
    int64_t     raw_value      = 0;
    double      eng_value      = 0.0;
    int         quality        = 0;
    std::string collect_time;
};

} // namespace db
} // namespace ems

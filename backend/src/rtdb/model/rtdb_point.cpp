/**
 * @file rtdb_point.cpp
 * @brief 运行时测点序列化（实现内联在 point_runtime.hpp 中）
 *
 * 运行时测点的序列化/反序列化已以内联方式实现
 * 在 src/models/point_runtime.hpp 中，使用 nlohmann::json。
 * 本文件仅包含编译依赖引入。
 */
#include "rtdb/model/rtdb_point.h"

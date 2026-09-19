/**
 * @file models.h
 * @brief EMS数据模型总头文件
 */
#ifndef EMS_MODELS_H
#define EMS_MODELS_H

#include "base.h"
#include "channel.h"
#include "device.h"
#include "device_group.h"
#include "point_mapping.h"
#include "point_runtime.hpp"
#include "slave.h"

namespace ems {
namespace model {

constexpr const char* VERSION = "1.0.0";
constexpr const char* MODEL_NAME = "EMS Data Models";

}  // namespace model
}  // namespace ems

#endif  // EMS_MODELS_H

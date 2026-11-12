/**
 * @file dlt698.hpp
 * @brief 核心库聚合头文件，包含字节工具、Data、APDU、链路帧及常用标准点位。
 */
#pragma once
#include <dlt698/codec/data_codec.hpp>
#include <dlt698/common/bytes.hpp>
#include <dlt698/common/md5.hpp>
#include <dlt698/protocol/apdu/apdu.hpp>
#include <dlt698/protocol/apdu/get.hpp>
#include <dlt698/protocol/apdu/get_block.hpp>
#include <dlt698/protocol/apdu/security.hpp>
#include <dlt698/protocol/apdu/time_tag.hpp>
#include <dlt698/protocol/link/fragment.hpp>
#include <dlt698/protocol/link/frame.hpp>
#include <dlt698/standard/capabilities.hpp>
#include <dlt698/standard/catalog.hpp>
#include <dlt698/standard/records.hpp>

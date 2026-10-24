#pragma once

#include <map>
#include <string>

#include "web/modules/device/application/device_service.h"

namespace ems::web {

/// 整站点表导入包：manifest.json（层级 + 通讯参数 + 文件映射）+ 每设备一个 xlsx。
struct StationPackage {
    Json manifest;                                   ///< 已通过结构校验的清单
    std::map<std::string, std::string> point_files;  ///< 设备 code → 点表 xlsx 字节
    std::string package_sha256;                      ///< 整包字节摘要（幂等键）
};

/**
 * @brief 解包并校验整站点表导入包。
 *
 * 使用 miniz 读取 zip；总解压预算与文件数上限复用点表 xlsx 的检查模式，
 * 阻止压缩炸弹。manifest 结构校验覆盖：schema 版本、code 合法性与唯一性、
 * 设备树父/采集引用闭合（引用只允许指向已定义节点，因此无环）、通道与
 * 绑定引用、点表文件存在性。硬错误抛 ApiError，调用方得到整包拒绝。
 * @param[in] upload multipart 上传的 zip 原始字节。
 * @return 解包结果；manifest 与文件集合。
 */
StationPackage parse_station_package(const Upload& upload);

}  // namespace ems::web

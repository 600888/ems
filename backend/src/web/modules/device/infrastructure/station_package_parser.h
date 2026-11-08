#pragma once

#include <map>
#include <string>

#include "web/modules/device/application/device_service.h"

namespace ems::web {

/// 整站点表导入包：manifest.json（层级、通信、归属）与可跨部件复用的 XLSX。
struct StationPackage {
    Json manifest;                                   ///< 已通过结构校验的清单
    std::map<std::string, std::string> point_files;  ///< 文件名 → 点表字节，允许跨簇复用
    std::map<std::pair<std::string, std::string>, Json>
        parsed_point_files;      ///< 文件名、协议 → 解析行
    std::string package_sha256;  ///< 整包字节摘要（幂等键）
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

/**
 * @brief 在工程事务之前按文件及协议解析可复用点表，每种组合只解析一次。
 * @param[in,out] package 结构已校验的包，填充 parsed_point_files。
 * @param[in] workspace 临时文件工作目录。
 */
void prepare_station_point_files(StationPackage& package, const std::string& workspace);

}  // namespace ems::web

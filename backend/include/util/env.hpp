#pragma once
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#ifndef VAR_ENV
#define VAR_ENV "EMS_ROOT"
#endif

#ifndef DEFAULT_ROOT_DIR
#define DEFAULT_ROOT_DIR "/home/cdy/ems/backend/build"
#endif

/**
 * @brief 返回 EMS_ROOT 环境变量指定的根目录，未设置时使用默认目录。
 * @return 对应的文本内容。
 */
inline std::string rootPath() {
    std::string rootDir;
    const char* envSecuRoot = getenv(VAR_ENV);
    if (nullptr == envSecuRoot) {
        rootDir = DEFAULT_ROOT_DIR;
    } else {
        rootDir = envSecuRoot;
    }

    return rootDir;
}

/**
 * @brief 返回根目录下的配置目录。
 * @return 对应的文本内容。
 */
inline std::string confBasePath() { return rootPath() + "/conf/"; }

/**
 * @brief 返回根目录下的数据库目录。
 * @return 对应的文本内容。
 */
inline std::string dbPath() { return rootPath() + "/db/"; }

/**
 * @brief 返回根目录下的动态库目录。
 * @return 对应的文本内容。
 */
inline std::string libPath() { return rootPath() + "/lib/"; }

/**
 * @brief 返回根目录下的数据目录。
 * @return 对应的文本内容。
 */
inline std::string dataPath() { return rootPath() + "/data/"; }

/**
 * @brief 返回协议动态库目录。
 * @return 对应的文本内容。
 */
inline std::string protocolPath() { return libPath(); }

/**
 * @brief 返回根目录下的日志目录。
 * @return 对应的文本内容。
 */
inline std::string logPath() { return rootPath() + "/log/"; }

/**
 * @brief 返回根目录下的运行时目录。
 * @return 对应的文本内容。
 */
inline std::string runPath() { return rootPath() + "/run/"; }

/**
 * @brief 返回根目录下的脚本目录。
 * @return 对应的文本内容。
 */
inline std::string scriptsPath() { return rootPath() + "/scripts/"; }

/**
 * @brief 返回根目录下的下载目录。
 * @return 对应的文本内容。
 */
inline std::string downloadBasePath() { return rootPath() + "/download/"; }

/**
 * @brief 返回根目录下的 Web 包目录。
 * @return 对应的文本内容。
 */
inline std::string wwwPackagePath() { return rootPath() + "/www/"; }

/// @brief modbus 转发配置路径
/// @return 对应的文本内容。
inline std::string transpondConfDir() {
    std::string path = confBasePath() + "/comm/protocol/transpond/";
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
    }
    return path;
}

/// @brief ems_mqtt_cloud 配置路径
/// @return 对应的文本内容。
inline std::string emsMqttCloudConfDir() {
    std::string path = confBasePath() + "/ems_mqtt_cloud/";
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
    }
    return path;
}

/// @brief ems_provd 配置路径
/// @return 对应的文本内容。
inline std::string emsProvdConfDir() {
    std::string path = confBasePath() + "/ems_provd/";
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
    }
    return path;
}

/// @brief 主接线图备份路径
/// @return 对应的文本内容。
inline std::string wiringBackupPath() {
    std::string path = dataPath() + "/wiring_backup/";
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
    }
    return path;
}

/// @brief python 脚本路径
/// @return 对应的文本内容。
inline std::string pythonScriptPath() {
    std::string path = rootPath() + "/python/";
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
    }
    return path;
}

/// @brief 策略模型python脚本路径
/// @return 对应的文本内容。
inline std::string strategyModelPythonScriptPath() {
    std::string path = pythonScriptPath() + "/proj/";
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
    }
    return path;
}

/// @brief ems升级文件路径
/// @return 对应的文本内容。
inline std::string emsUpgradeFolderPath() {
    std::string path = rootPath() + "/upgrade/";
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
    }
    return path;
}

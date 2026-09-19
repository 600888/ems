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

inline std::string confBasePath() { return rootPath() + "/conf/"; }

inline std::string dbPath() { return rootPath() + "/db/"; }

inline std::string libPath() { return rootPath() + "/lib/"; }

inline std::string dataPath() { return rootPath() + "/data/"; }

inline std::string protocolPath() { return libPath(); }

inline std::string logPath() { return rootPath() + "/log/"; }

inline std::string runPath() { return rootPath() + "/run/"; }

inline std::string scriptsPath() { return rootPath() + "/scripts/"; }

inline std::string downloadBasePath() { return rootPath() + "/download/"; }

inline std::string wwwPackagePath() { return rootPath() + "/www/"; }

/// @brief modbus 转发配置路径
inline std::string transpondConfDir() {
    std::string path = confBasePath() + "/comm/protocol/transpond/";
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
    }
    return path;
}

/// @brief ems_mqtt_cloud 配置路径
inline std::string emsMqttCloudConfDir() {
    std::string path = confBasePath() + "/ems_mqtt_cloud/";
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
    }
    return path;
}

/// @brief ems_provd 配置路径
inline std::string emsProvdConfDir() {
    std::string path = confBasePath() + "/ems_provd/";
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
    }
    return path;
}

/// @brief 主接线图备份路径
inline std::string wiringBackupPath() {
    std::string path = dataPath() + "/wiring_backup/";
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
    }
    return path;
}

/// @brief python 脚本路径
inline std::string pythonScriptPath() {
    std::string path = rootPath() + "/python/";
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
    }
    return path;
}

/// @brief 策略模型python脚本路径
inline std::string strategyModelPythonScriptPath() {
    std::string path = pythonScriptPath() + "/proj/";
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
    }
    return path;
}

/// @brief ems升级文件路径
inline std::string emsUpgradeFolderPath() {
    std::string path = rootPath() + "/upgrade/";
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
    }
    return path;
}

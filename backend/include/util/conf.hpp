#pragma once
#include <string>

#include "env.hpp"

// daemon配置文件
inline std::string emsdConfPath() { return confBasePath() + "/proc_manage.json"; }

// 日志配置文件
inline std::string logConfPath() { return confBasePath() + "/log.json"; }

// 电站配置文件
inline std::string stationConfPath() { return confBasePath() + "/station.json"; }

// 数据库配置文件
inline std::string dbConfPath() { return confBasePath() + "/db.json"; }

// rpc配置文件
inline std::string rpcConfPath() { return confBasePath() + "/rpc.json"; }

// 主接线图文件
inline std::string wiringDiagramPath() { return dataPath() + "/wiring_diagram.json"; }

// 通道协议配置文件
inline std::string protocol104ConfPath() { return confBasePath() + "/protocol_104/"; }

// 统计配置文件
inline std::string scdBasePath() { return confBasePath() + "/scd/"; }

inline std::string scdConfPath() { return scdBasePath() + "/scd.json"; }

inline std::string scdEventTopicPath() { return scdBasePath() + "/topic.json"; }

inline std::string scdDeletePlanPath() { return scdBasePath() + "/delete_plan.json"; }

// webServer配置文件
inline std::string webServerBasePath() { return confBasePath() + "/web_server/"; }

inline std::string webServerConfPath() { return webServerBasePath() + "/web_server.json"; }

// mqtt配置文件
inline std::string mqttBasePath() { return confBasePath() + "/mqtt/"; }
inline std::string mqttTestConfPath() { return mqttBasePath() + "/test/"; }
inline std::string mqttBrokerConfPath() { return mqttTestConfPath() + "/mqtt_broker.conf"; }
inline std::string mqttClientConfPath() { return mqttTestConfPath() + "/mqtt_client.conf"; }
inline std::string mqttConfPath() { return mqttBasePath() + "/mqtt.json"; }

// 统计电量生成文件
inline std::string scdElectricKwhReportPath() { return dataPath() + "/electric_kwh_report.json"; }

// 中英文标识文件
inline std::string languageConfPath() { return confBasePath() + "/language.json"; }

/// @brief mqtt2Cloud 配置文件完整路径
inline std::string emsMqttCloudConfPath() { return emsMqttCloudConfDir() + "/ems_mqtt_cloud.json"; }

/// @brief ems_provd 配置文件完整路径
inline std::string emsProvdConfPath() { return emsProvdConfDir() + "/ems_provd.json"; }

// ems版本文件
inline std::string emsVersionFilePath() { return rootPath() + "/ems_version"; }
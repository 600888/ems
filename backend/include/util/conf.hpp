#pragma once
#include <string>

#include "env.hpp"

/**
 * @brief 返回进程管理配置文件路径。
 * @return 对应的文本内容。
 */
inline std::string emsdConfPath() { return confBasePath() + "/proc_manage.json"; }

/**
 * @brief 返回日志配置文件路径。
 * @return 对应的文本内容。
 */
inline std::string logConfPath() { return confBasePath() + "/log.json"; }

/**
 * @brief 返回电站配置文件路径。
 * @return 对应的文本内容。
 */
inline std::string stationConfPath() { return confBasePath() + "/station.json"; }

/**
 * @brief 返回数据库配置文件路径。
 * @return 对应的文本内容。
 */
inline std::string dbConfPath() { return confBasePath() + "/db.json"; }

/**
 * @brief 返回 RPC 配置文件路径。
 * @return 对应的文本内容。
 */
inline std::string rpcConfPath() { return confBasePath() + "/rpc.json"; }

/**
 * @brief 返回主接线图数据文件路径。
 * @return 对应的文本内容。
 */
inline std::string wiringDiagramPath() { return dataPath() + "/wiring_diagram.json"; }

/**
 * @brief 返回 IEC 104 协议配置目录。
 * @return 对应的文本内容。
 */
inline std::string protocol104ConfPath() { return confBasePath() + "/protocol_104/"; }

/**
 * @brief 返回统计配置目录。
 * @return 对应的文本内容。
 */
inline std::string scdBasePath() { return confBasePath() + "/scd/"; }

/**
 * @brief 返回统计配置文件路径。
 * @return 对应的文本内容。
 */
inline std::string scdConfPath() { return scdBasePath() + "/scd.json"; }

/**
 * @brief 返回统计事件主题配置文件路径。
 * @return 对应的文本内容。
 */
inline std::string scdEventTopicPath() { return scdBasePath() + "/topic.json"; }

/**
 * @brief 返回统计删除计划文件路径。
 * @return 对应的文本内容。
 */
inline std::string scdDeletePlanPath() { return scdBasePath() + "/delete_plan.json"; }

/**
 * @brief 返回 Web 服务配置目录。
 * @return 对应的文本内容。
 */
inline std::string webServerBasePath() { return confBasePath() + "/web_server/"; }

/**
 * @brief 返回 Web 服务配置文件路径。
 * @return 对应的文本内容。
 */
inline std::string webServerConfPath() { return webServerBasePath() + "/web_server.json"; }

/**
 * @brief 返回 MQTT 配置目录。
 * @return 对应的文本内容。
 */
inline std::string mqttBasePath() { return confBasePath() + "/mqtt/"; }

/**
 * @brief 返回 MQTT 测试配置目录。
 * @return 对应的文本内容。
 */
inline std::string mqttTestConfPath() { return mqttBasePath() + "/test/"; }

/**
 * @brief 返回 MQTT broker 配置文件路径。
 * @return 对应的文本内容。
 */
inline std::string mqttBrokerConfPath() { return mqttTestConfPath() + "/mqtt_broker.conf"; }

/**
 * @brief 返回 MQTT 客户端配置文件路径。
 * @return 对应的文本内容。
 */
inline std::string mqttClientConfPath() { return mqttTestConfPath() + "/mqtt_client.conf"; }

/**
 * @brief 返回 MQTT 配置文件路径。
 * @return 对应的文本内容。
 */
inline std::string mqttConfPath() { return mqttBasePath() + "/mqtt.json"; }

/**
 * @brief 返回电量统计报告文件路径。
 * @return 对应的文本内容。
 */
inline std::string scdElectricKwhReportPath() { return dataPath() + "/electric_kwh_report.json"; }

/**
 * @brief 返回语言配置文件路径。
 * @return 对应的文本内容。
 */
inline std::string languageConfPath() { return confBasePath() + "/language.json"; }

/// @brief mqtt2Cloud 配置文件完整路径
/// @return 对应的文本内容。
inline std::string emsMqttCloudConfPath() { return emsMqttCloudConfDir() + "/ems_mqtt_cloud.json"; }

/// @brief ems_provd 配置文件完整路径
/// @return 对应的文本内容。
inline std::string emsProvdConfPath() { return emsProvdConfDir() + "/ems_provd.json"; }

/**
 * @brief 返回 EMS 版本标识文件路径。
 * @return 对应的文本内容。
 */
inline std::string emsVersionFilePath() { return rootPath() + "/ems_version"; }
#include "rtdb/rtdb_manager.h"
#include "rtdb/rtdb_cache.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <map>

using namespace ems::net;

void example_basic_operations() {
    std::cout << "\n=== 基础操作示例 ===" << std::endl;
    
    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";
    config.pool_size = 10;
    
    auto manager = createRTDBManager(config);
    if (!manager || !manager->isConnected()) {
        std::cerr << "连接Redis失败: " << manager->getLastError() << std::endl;
        return;
    }
    
    std::cout << "✓ Redis连接成功" << std::endl;
    
    auto config_cache = manager->getConfigCache();
    std::vector<RTDBConfigCache::YCConfig> yc_configs = {
        {"voltage_a", "A相电压", 1, 0.1, 0.0, 250.0, 0.0, "V"},
        {"voltage_b", "B相电压", 1, 0.1, 0.0, 250.0, 0.0, "V"},
        {"voltage_c", "C相电压", 1, 0.1, 0.0, 250.0, 0.0, "V"},
        {"current_a", "A相电流", 1, 0.01, 0.0, 100.0, 0.0, "A"},
    };
    config_cache->loadYCConfigs(yc_configs);
    
    std::cout << "✓ 加载 " << config_cache->getYCConfigCount() << " 个遥测配置" << std::endl;
    
    std::vector<RTDBConfigCache::YXConfig> yx_configs = {
        {"breaker_001", "1号断路器", 1, 0, false},
        {"breaker_002", "2号断路器", 1, 1, false},
        {"breaker_003", "3号断路器", 1, 2, false},
    };
    config_cache->loadYXConfigs(yx_configs);
    std::cout << "✓ 加载 " << config_cache->getYXConfigCount() << " 个遥信配置" << std::endl;
}

void example_update_and_query() {
    std::cout << "\n=== 更新和查询示例 ===" << std::endl;
    
    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";
    
    auto manager = createRTDBManager(config);
    if (!manager || !manager->isConnected()) {
        std::cerr << "连接Redis失败" << std::endl;
        return;
    }
    
    auto config_cache = manager->getConfigCache();
    
    std::vector<RTDBConfigCache::YCConfig> yc_configs = {
        {"voltage_a", "A相电压", 1, 0.1, 0.0, 250.0, 0.0, "V"},
        {"voltage_b", "B相电压", 1, 0.1, 0.0, 250.0, 0.0, "V"},
    };
    config_cache->loadYCConfigs(yc_configs);
    
    YCPoint yc;
    yc.value = 220.5;
    yc.timestamp = std::time(nullptr);
    yc.quality = 0;
    yc.status = 0;
    
    if (manager->updateYC("voltage_a", yc)) {
        std::cout << "✓ 遥测点voltage_a更新成功: " << yc.value << "V" << std::endl;
    }
    
    YCPoint read_yc;
    if (manager->getYC("voltage_a", read_yc)) {
        std::cout << "✓ 遥测点voltage_a读取成功: " << read_yc.value << "V" << std::endl;
    }
    
    YXPoint yx;
    yx.value = 1;
    yx.timestamp = std::time(nullptr);
    yx.quality = 0;
    yx.status = 0;
    
    if (manager->updateYX("breaker_001", yx)) {
        std::cout << "✓ 遥信点breaker_001更新成功: " << yx.value << std::endl;
    }
}

void example_full_point() {
    std::cout << "\n=== 完整测点示例（配置+数据）===" << std::endl;
    
    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";
    
    auto manager = createRTDBManager(config);
    if (!manager || !manager->isConnected()) {
        std::cerr << "连接Redis失败" << std::endl;
        return;
    }
    
    auto config_cache = manager->getConfigCache();
    
    std::vector<RTDBConfigCache::YCConfig> yc_configs = {
        {"voltage_a", "A相电压", 1, 0.1, 0.0, 250.0, 0.0, "V"},
        {"current_a", "A相电流", 1, 0.01, 0.0, 100.0, 0.0, "A"},
    };
    config_cache->loadYCConfigs(yc_configs);
    
    YCPoint yc_data;
    yc_data.value = 220.5;
    yc_data.timestamp = std::time(nullptr);
    yc_data.quality = 0;
    manager->updateYC("voltage_a", yc_data);
    
    RTDBPointManager::YCFullPoint full_point;
    if (manager->getYCFull("voltage_a", full_point)) {
        std::cout << "测点代码: " << full_point.config.code << std::endl;
        std::cout << "测点名称: " << full_point.config.name << std::endl;
        std::cout << "实时值: " << full_point.data.value << full_point.config.unit << std::endl;
        std::cout << "质量: " << (full_point.data.quality == 0 ? "好" : "坏") << std::endl;
        std::cout << "时间戳: " << full_point.data.timestamp << std::endl;
        std::cout << "上限: " << full_point.config.max_limit << full_point.config.unit << std::endl;
        std::cout << "下限: " << full_point.config.min_limit << full_point.config.unit << std::endl;
    }
}

void example_batch_operations() {
    std::cout << "\n=== 批量操作示例 ===" << std::endl;
    
    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";
    
    auto manager = createRTDBManager(config);
    if (!manager || !manager->isConnected()) {
        std::cerr << "连接Redis失败" << std::endl;
        return;
    }
    
    auto config_cache = manager->getConfigCache();
    
    std::vector<RTDBConfigCache::YCConfig> yc_configs;
    for (int i = 0; i < 10; ++i) {
        yc_configs.push_back({
            "voltage_" + std::to_string(i),
            "电压_" + std::to_string(i),
            1, 0.1, 0.0, 250.0, 0.0, "V"
        });
    }
    config_cache->loadYCConfigs(yc_configs);
    
    std::map<std::string, YCPoint> yc_batch;
    for (int i = 0; i < 10; ++i) {
        YCPoint yc;
        yc.value = 200.0 + i * 10.0;
        yc.timestamp = std::time(nullptr);
        yc.quality = 0;
        yc.status = 0;
        yc_batch["voltage_" + std::to_string(i)] = yc;
    }
    
    if (manager->batchUpdateYC(yc_batch)) {
        std::cout << "✓ 批量更新 " << yc_batch.size() << " 个遥测点成功" << std::endl;
    }
    
    std::cout << "YC测点数量: " << manager->getYCDataCount() << std::endl;
}

void example_data_change_notification() {
    std::cout << "\n=== 数据变更通知示例 ===" << std::endl;
    
    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";
    
    auto manager = createRTDBManager(config);
    if (!manager || !manager->isConnected()) {
        std::cerr << "连接Redis失败" << std::endl;
        return;
    }
    
    manager->setDataChangeCallback([](const std::string& point_type,
                                     const std::string& point_code,
                                     const void* data) {
        std::cout << "数据变更: 类型=" << point_type 
                  << ", 代码=" << point_code << std::endl;
    });
    
    YCPoint yc;
    yc.value = 220.0;
    yc.timestamp = std::time(nullptr);
    yc.quality = 0;
    manager->updateYC("voltage_a", yc);
    
    std::cout << "✓ 已设置数据变更回调" << std::endl;
}

void example_continuous_monitoring() {
    std::cout << "\n=== 连续监控示例 ===" << std::endl;
    
    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";
    
    auto manager = createRTDBManager(config);
    if (!manager || !manager->isConnected()) {
        std::cerr << "连接Redis失败" << std::endl;
        return;
    }
    
    auto config_cache = manager->getConfigCache();
    
    std::vector<RTDBConfigCache::YCConfig> yc_configs = {
        {"voltage_a", "A相电压", 1, 1.0, 0.0, 250.0, 0.0, "V"},
        {"current_a", "A相电流", 1, 1.0, 0.0, 100.0, 0.0, "A"},
    };
    config_cache->loadYCConfigs(yc_configs);
    
    std::cout << "开始监控（5秒）..." << std::endl;
    for (int i = 0; i < 5; ++i) {
        YCPoint yc;
        yc.value = 220.0 + (i % 2) * 5.0;
        yc.timestamp = std::time(nullptr);
        yc.quality = 0;
        manager->updateYC("voltage_a", yc);
        
        YCPoint read_yc;
        if (manager->getYC("voltage_a", read_yc)) {
            std::cout << "  [" << i << "] voltage_a = " << read_yc.value << "V" << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "监控完成" << std::endl;
}

void example_alarm_detection() {
    std::cout << "\n=== 告警检测示例 ===" << std::endl;
    
    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";
    
    auto manager = createRTDBManager(config);
    if (!manager || !manager->isConnected()) {
        std::cerr << "连接Redis失败" << std::endl;
        return;
    }
    
    auto point_manager = manager->getPointManager();
    auto config_cache = manager->getConfigCache();
    
    std::vector<RTDBConfigCache::YCConfig> yc_configs = {
        {"voltage_a", "A相电压", 1, 1.0, 0.0, 240.0, 180.0, "V"},
    };
    config_cache->loadYCConfigs(yc_configs);
    
    YCPoint yc;
    yc.value = 260.0;
    yc.timestamp = std::time(nullptr);
    yc.quality = 0;
    manager->updateYC("voltage_a", yc);
    
    RTDBPointManager::YCFullPoint full_point;
    if (manager->getYCFull("voltage_a", full_point)) {
        bool is_valid = point_manager->validateYCValue(
            full_point.data.value, 
            full_point.config
        );
        
        std::cout << "电压: " << full_point.data.value << full_point.config.unit << std::endl;
        std::cout << "上限: " << full_point.config.max_limit << full_point.config.unit << std::endl;
        std::cout << "下限: " << full_point.config.min_limit << full_point.config.unit << std::endl;
        std::cout << "告警: " << (is_valid ? "无" : "超出范围!") << std::endl;
    }
}

int main() {
    std::cout << "======================================" << std::endl;
    std::cout << "EMS RTDB Manager 使用示例" << std::endl;
    std::cout << "======================================" << std::endl;
    
    try {
        example_basic_operations();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        example_update_and_query();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        example_full_point();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        example_batch_operations();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        example_data_change_notification();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        example_continuous_monitoring();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        example_alarm_detection();
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n======================================" << std::endl;
    std::cout << "示例完成!" << std::endl;
    std::cout << "======================================" << std::endl;
    
    return 0;
}

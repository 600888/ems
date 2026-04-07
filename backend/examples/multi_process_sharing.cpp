#include "rtdb/rtdb.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <map>
#include <memory>
#include <future>
#include <fstream>
#include <sstream>

using namespace ems::net;

namespace {

struct DeviceConfig {
    std::string device_id;
    std::string device_name;
    std::string protocol;
    std::string host;
    int port;
    std::vector<std::string> yc_codes;
    std::vector<std::string> yx_codes;
    std::vector<std::string> yk_codes;
    std::vector<std::string> yt_codes;
};

class ConfigLoader {
public:
    static std::vector<DeviceConfig> loadFromJSON(const std::string& file_path) {
        std::vector<DeviceConfig> devices;
        
        std::ifstream file(file_path);
        if (!file.is_open()) {
            std::cerr << "Failed to open config file: " << file_path << std::endl;
            return devices;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        
        std::cout << "[ConfigLoader] Loaded config from: " << file_path << std::endl;
        return devices;
    }
    
    static std::vector<DeviceConfig> getDemoDevices() {
        std::vector<DeviceConfig> devices;
        
        DeviceConfig dev1;
        dev1.device_id = "dev_001";
        dev1.device_name = "1号变压器";
        dev1.protocol = "modbus_tcp";
        dev1.host = "192.168.1.100";
        dev1.port = 502;
        dev1.yc_codes = {"voltage_a", "voltage_b", "voltage_c", "current_a", "current_b", "current_c"};
        dev1.yx_codes = {"breaker_001", "breaker_002"};
        dev1.yk_codes = {"yk_breaker_001"};
        dev1.yt_codes = {"setpoint_voltage"};
        devices.push_back(dev1);
        
        DeviceConfig dev2;
        dev2.device_id = "dev_002";
        dev2.device_name = "2号母线";
        dev2.protocol = "iec104";
        dev2.host = "192.168.1.101";
        dev2.port = 2404;
        dev2.yc_codes = {"bus_voltage", "frequency", "power"};
        dev2.yx_codes = {"relay_status"};
        devices.push_back(dev2);
        
        return devices;
    }
};

class PointModelCache {
public:
    struct YCModel {
        std::string code;
        std::string name;
        std::string device_id;
        double mul_coe{1.0};
        double add_coe{0.0};
        double max_limit{9999999.0};
        double min_limit{-9999999.0};
        std::string unit{"-"};
    };
    
    struct YXModel {
        std::string code;
        std::string name;
        std::string device_id;
        int bit{0};
        bool reverse{false};
    };
    
    struct YKModel {
        std::string code;
        std::string name;
        std::string device_id;
        int command_type{0};
        int64_t related_yx_id{0};
    };
    
    struct YTModel {
        std::string code;
        std::string name;
        std::string device_id;
        double mul_coe{1.0};
        double add_coe{0.0};
        double max_limit{9999999.0};
        double min_limit{-9999999.0};
        std::string unit{"-"};
    };
    
    void loadFromDevices(const std::vector<DeviceConfig>& devices) {
        for (const auto& dev : devices) {
            for (size_t i = 0; i < dev.yc_codes.size(); ++i) {
                YCModel model;
                model.code = dev.yc_codes[i];
                model.name = dev.device_name + "_YC" + std::to_string(i + 1);
                model.device_id = dev.device_id;
                model.mul_coe = 1.0;
                model.add_coe = 0.0;
                model.max_limit = 250.0;
                model.min_limit = 0.0;
                model.unit = (model.code.find("voltage") != std::string::npos) ? "V" : "A";
                
                yc_models_[model.code] = model;
            }
            
            for (size_t i = 0; i < dev.yx_codes.size(); ++i) {
                YXModel model;
                model.code = dev.yx_codes[i];
                model.name = dev.device_name + "_YX" + std::to_string(i + 1);
                model.device_id = dev.device_id;
                model.bit = i;
                model.reverse = false;
                
                yx_models_[model.code] = model;
            }
            
            for (const auto& code : dev.yk_codes) {
                YKModel model;
                model.code = code;
                model.name = dev.device_name + "_YK";
                model.device_id = dev.device_id;
                model.command_type = 0;
                
                yk_models_[model.code] = model;
            }
            
            for (const auto& code : dev.yt_codes) {
                YTModel model;
                model.code = code;
                model.name = dev.device_name + "_YT";
                model.device_id = dev.device_id;
                model.mul_coe = 1.0;
                model.add_coe = 0.0;
                model.max_limit = 100.0;
                model.min_limit = 0.0;
                model.unit = "%";
                
                yt_models_[model.code] = model;
            }
        }
    }
    
    bool getYCModel(const std::string& code, YCModel& model) const {
        auto it = yc_models_.find(code);
        if (it != yc_models_.end()) {
            model = it->second;
            return true;
        }
        return false;
    }
    
    bool getYXModel(const std::string& code, YXModel& model) const {
        auto it = yx_models_.find(code);
        if (it != yx_models_.end()) {
            model = it->second;
            return true;
        }
        return false;
    }
    
    size_t getYCCount() const { return yc_models_.size(); }
    size_t getYXCount() const { return yx_models_.size(); }
    size_t getYKCount() const { return yk_models_.size(); }
    size_t getYTCount() const { return yt_models_.size(); }
    
private:
    std::map<std::string, YCModel> yc_models_;
    std::map<std::string, YXModel> yx_models_;
    std::map<std::string, YKModel> yk_models_;
    std::map<std::string, YTModel> yt_models_;
};

class MultiProcessRTDBManager {
public:
    MultiProcessRTDBManager(const RTDBConfig& config) 
        : config_(config), is_initialized_(false) {
    }
    
    bool initialize() {
        rtdb_ = createRTDB(config_);
        if (!rtdb_ || !rtdb_->isConnected()) {
            std::cerr << "[Manager] Failed to connect to Redis" << std::endl;
            return false;
        }
        
        model_cache_ = std::make_shared<PointModelCache>();
        loadDeviceModels();
        
        is_initialized_ = true;
        std::cout << "[Manager] Initialized successfully" << std::endl;
        return true;
    }
    
    void loadDeviceModels() {
        auto devices = ConfigLoader::getDemoDevices();
        model_cache_->loadFromDevices(devices);
        
        std::cout << "[Manager] Loaded device models:" << std::endl;
        std::cout << "  - YC: " << model_cache_->getYCCount() << std::endl;
        std::cout << "  - YX: " << model_cache_->getYXCount() << std::endl;
        std::cout << "  - YK: " << model_cache_->getYKCount() << std::endl;
        std::cout << "  - YT: " << model_cache_->getYTCount() << std::endl;
    }
    
    bool updateYCValue(const std::string& code, double value, int quality = 0) {
        YCPoint point;
        point.value = value;
        point.timestamp = static_cast<uint64_t>(std::time(nullptr));
        point.quality = quality;
        point.status = 0;
        
        return rtdb_->setYC(code, point);
    }
    
    bool updateYXValue(const std::string& code, int value, int quality = 0) {
        YXPoint point;
        point.value = value;
        point.timestamp = static_cast<uint64_t>(std::time(nullptr));
        point.quality = quality;
        point.status = 0;
        
        return rtdb_->setYX(code, point);
    }
    
    struct FullYCPoint {
        PointModelCache::YCModel model;
        YCPoint data;
    };
    
    bool getYCFull(const std::string& code, FullYCPoint& full_point) {
        PointModelCache::YCModel model;
        if (!model_cache_->getYCModel(code, model)) {
            return false;
        }
        
        YCPoint data;
        if (!rtdb_->getYC(code, data)) {
            return false;
        }
        
        full_point.model = model;
        full_point.data = data;
        return true;
    }
    
    struct FullYXPoint {
        PointModelCache::YXModel model;
        YXPoint data;
    };
    
    bool getYXFull(const std::string& code, FullYXPoint& full_point) {
        PointModelCache::YXModel model;
        if (!model_cache_->getYXModel(code, model)) {
            return false;
        }
        
        YXPoint data;
        if (!rtdb_->getYX(code, data)) {
            return false;
        }
        
        full_point.model = model;
        full_point.data = data;
        return true;
    }
    
    bool isInitialized() const { return is_initialized_; }
    std::shared_ptr<PointModelCache> getModelCache() const { return model_cache_; }
    
private:
    RTDBConfig config_;
    std::shared_ptr<RTDB> rtdb_;
    std::shared_ptr<PointModelCache> model_cache_;
    bool is_initialized_;
};

}

void process_data_collector() {
    std::cout << "\n[Process: Data Collector] Started (PID: " << getpid() << ")" << std::endl;
    
    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";
    config.pool_size = 5;
    
    MultiProcessRTDBManager manager(config);
    if (!manager.initialize()) {
        std::cerr << "[Collector] Failed to initialize" << std::endl;
        return;
    }
    
    std::cout << "[Collector] Starting data collection..." << std::endl;
    
    int iteration = 0;
    while (iteration < 10) {
        auto yc_codes = {"voltage_a", "voltage_b", "voltage_c"};
        auto yx_codes = {"breaker_001", "breaker_002"};
        
        for (const auto& code : yc_codes) {
            double value = 220.0 + (iteration % 2) * 10.0 + (rand() % 20 - 10);
            if (manager.updateYCValue(code, value)) {
                std::cout << "[Collector] Updated YC: " << code 
                         << " = " << value << std::endl;
            }
        }
        
        for (const auto& code : yx_codes) {
            int value = (iteration % 2 == 0) ? 1 : 0;
            if (manager.updateYXValue(code, value)) {
                std::cout << "[Collector] Updated YX: " << code 
                         << " = " << value << std::endl;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
        iteration++;
    }
    
    std::cout << "[Process: Data Collector] Finished" << std::endl;
}

void process_api_server() {
    std::cout << "\n[Process: API Server] Started (PID: " << getpid() << ")" << std::endl;
    
    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";
    config.pool_size = 10;
    
    MultiProcessRTDBManager manager(config);
    if (!manager.initialize()) {
        std::cerr << "[API] Failed to initialize" << std::endl;
        return;
    }
    
    std::cout << "[API] Querying data..." << std::endl;
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    MultiProcessRTDBManager::FullYCPoint yc_full;
    if (manager.getYCFull("voltage_a", yc_full)) {
        std::cout << "\n[API] === YC Point Details ===" << std::endl;
        std::cout << "  Code: " << yc_full.model.code << std::endl;
        std::cout << "  Name: " << yc_full.model.name << std::endl;
        std::cout << "  Device: " << yc_full.model.device_id << std::endl;
        std::cout << "  Value: " << yc_full.data.value << " " << yc_full.model.unit << std::endl;
        std::cout << "  Timestamp: " << yc_full.data.timestamp << std::endl;
        std::cout << "  Quality: " << (yc_full.data.quality == 0 ? "Good" : "Bad") << std::endl;
        std::cout << "  Limits: " << yc_full.model.min_limit << " ~ " 
                  << yc_full.model.max_limit << " " << yc_full.model.unit << std::endl;
    }
    
    MultiProcessRTDBManager::FullYXPoint yx_full;
    if (manager.getYXFull("breaker_001", yx_full)) {
        std::cout << "\n[API] === YX Point Details ===" << std::endl;
        std::cout << "  Code: " << yx_full.model.code << std::endl;
        std::cout << "  Name: " << yx_full.model.name << std::endl;
        std::cout << "  Device: " << yx_full.model.device_id << std::endl;
        std::cout << "  Value: " << (yx_full.data.value == 1 ? "闭合" : "断开") << std::endl;
        std::cout << "  Timestamp: " << yx_full.data.timestamp << std::endl;
        std::cout << "  Quality: " << (yx_full.data.quality == 0 ? "Good" : "Bad") << std::endl;
    }
    
    std::cout << "[Process: API Server] Finished" << std::endl;
}

void process_alarm_monitor() {
    std::cout << "\n[Process: Alarm Monitor] Started (PID: " << getpid() << ")" << std::endl;
    
    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";
    config.pool_size = 5;
    
    MultiProcessRTDBManager manager(config);
    if (!manager.initialize()) {
        std::cerr << "[Alarm] Failed to initialize" << std::endl;
        return;
    }
    
    std::cout << "[Alarm] Monitoring for alarms..." << std::endl;
    
    for (int i = 0; i < 5; ++i) {
        MultiProcessRTDBManager::FullYCPoint yc_full;
        if (manager.getYCFull("voltage_a", yc_full)) {
            bool out_of_range = yc_full.data.value > yc_full.model.max_limit ||
                               yc_full.data.value < yc_full.model.min_limit;
            
            if (out_of_range) {
                std::cout << "\n[ALARM!] Voltage out of range!" << std::endl;
                std::cout << "  Point: " << yc_full.model.name << std::endl;
                std::cout << "  Value: " << yc_full.data.value << " " << yc_full.model.unit << std::endl;
                std::cout << "  Limits: " << yc_full.model.min_limit << " ~ " 
                          << yc_full.model.max_limit << " " << yc_full.model.unit << std::endl;
            } else {
                std::cout << "[Alarm] Voltage OK: " << yc_full.data.value << " " 
                         << yc_full.model.unit << std::endl;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    std::cout << "[Process: Alarm Monitor] Finished" << std::endl;
}

void example_sequential() {
    std::cout << "\n=== Sequential Multi-Process Simulation ===" << std::endl;
    
    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";
    config.pool_size = 10;
    
    MultiProcessRTDBManager manager(config);
    if (!manager.initialize()) {
        std::cerr << "[Main] Failed to initialize manager" << std::endl;
        return;
    }
    
    std::cout << "\n[1/3] Simulating Data Collector..." << std::endl;
    manager.updateYCValue("voltage_a", 225.5);
    manager.updateYXValue("breaker_001", 1);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    std::cout << "\n[2/3] Simulating API Server Query..." << std::endl;
    MultiProcessRTDBManager::FullYCPoint yc_full;
    if (manager.getYCFull("voltage_a", yc_full)) {
        std::cout << "  Voltage: " << yc_full.data.value << " " << yc_full.model.unit << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    std::cout << "\n[3/3] Simulating Alarm Monitor..." << std::endl;
    MultiProcessRTDBManager::FullYXPoint yx_full;
    if (manager.getYXFull("breaker_001", yx_full)) {
        std::cout << "  Breaker: " << (yx_full.data.value == 1 ? "ON" : "OFF") << std::endl;
    }
}

void example_parallel() {
    std::cout << "\n=== Parallel Multi-Process Simulation ===" << std::endl;
    
    std::cout << "Starting 3 processes in parallel..." << std::endl;
    
    auto future_collector = std::async(std::launch::async, process_data_collector);
    auto future_api = std::async(std::launch::async, process_api_server);
    auto future_alarm = std::async(std::launch::async, process_alarm_monitor);
    
    future_collector.wait();
    future_api.wait();
    future_alarm.wait();
    
    std::cout << "\nAll processes completed!" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "=================================================" << std::endl;
    std::cout << "EMS Multi-Process RTDB Sharing Example" << std::endl;
    std::cout << "=================================================" << std::endl;
    
    try {
        if (argc > 1 && std::string(argv[1]) == "--parallel") {
            example_parallel();
        } else {
            example_sequential();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n=================================================" << std::endl;
    std::cout << "Example completed!" << std::endl;
    std::cout << "=================================================" << std::endl;
    
    return 0;
}

/**
 * @file data_collect_example.cpp
 * @brief 数据采集全流程示例
 *
 * 展示从数据库加载通道/设备/测点配置，到启动采集引擎，再到数据分发的完整流程。
 *
 * 编译:
 *   cmake --build . --target data_collect_example
 *
 * 运行:
 *   ./data_collect_example
 *   ./data_collect_example --db-pass mypass --db-name ems
 *   ./data_collect_example --db-host 192.168.1.100 --poll-ms 2000 --cycles 10
 */
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "db/channel.h"
#include "db/db_manager.h"
#include "db/device.h"
#include "db/point.h"
#include "net/collect/channel_thread.h"
#include "net/collect/collect_engine.h"
#include "net/collect/data_dispatcher.h"
#include "net/collect/plugin_manager.h"
#include "net/para_loader/para_loader.h"
#include "rtdb/rtdb_channel.h"

namespace fs = std::filesystem;
using ems::net::device::ParaLoader;
using ems::rtdb::ChannelConfig;

// ============================================================
// 默认配置
// ============================================================
static constexpr const char* DEFAULT_DB_HOST = "127.0.0.1";
static constexpr int DEFAULT_DB_PORT = 3306;
static constexpr const char* DEFAULT_DB_USER = "cdy";
static constexpr const char* DEFAULT_DB_PASS = "123456";
static constexpr const char* DEFAULT_DB_NAME = "ems";
static constexpr const char* DEFAULT_REDIS_HOST = "127.0.0.1";
static constexpr int DEFAULT_REDIS_PORT = 6379;
static constexpr int DEFAULT_POLL_MS = 1000;
static constexpr int DEFAULT_CYCLES = 5;

static std::atomic<bool> g_running{true};
static std::shared_ptr<ems::net::collect::CollectEngine> g_engine;

void signalHandler(int) {
    g_running = false;
    // 不在此处调 stop_all()，避免 join() 阻塞在底层 socket 调用上
    // 主循环检测到 g_running=false 后自动清理
}

static void print_usage(const char* prog) {
    std::cout << "用法: " << prog << " [选项]\n\n"
              << "默认: 直接运行使用本地开发环境\n\n"
              << "  --db-host   <host>   数据库地址 (默认: " << DEFAULT_DB_HOST << ")\n"
              << "  --db-port   <port>   数据库端口 (默认: " << DEFAULT_DB_PORT << ")\n"
              << "  --db-user   <user>   数据库用户 (默认: " << DEFAULT_DB_USER << ")\n"
              << "  --db-pass   <pass>   数据库密码 (默认: " << DEFAULT_DB_PASS << ")\n"
              << "  --db-name   <name>   数据库名称 (默认: " << DEFAULT_DB_NAME << ")\n"
              << "  --redis-host <host>  Redis 地址 (默认: " << DEFAULT_REDIS_HOST << ")\n"
              << "  --redis-port <port>  Redis 端口 (默认: " << DEFAULT_REDIS_PORT << ")\n"
              << "  --zmq-endpoint <uri>  用 ZeroMQ 发布可丢弃变更通知\n"
              << "  --modbus-ip  <addr>  Modbus 服务器地址 (默认: 从数据库读取)\n"
              << "  --modbus-port <port> Modbus 端口 (默认: 从数据库读取)\n"
              << "  --poll-ms    <ms>    采集周期 (默认: " << DEFAULT_POLL_MS << ")\n"
              << "  --cycles     <n>     采集轮次 (默认: " << DEFAULT_CYCLES << ", 0=无限)\n"
              << "  --help, -h           显示帮助\n\n"
              << "示例:\n"
              << "  " << prog << "\n"
              << "  " << prog << " --db-pass mypass --db-name ems --cycles 3\n"
              << "  " << prog << " --modbus-ip 192.168.1.100 --modbus-port 10502\n";
}

// ============================================================
// 解析命令行参数
// ============================================================

struct AppConfig {
    std::string dbHost{DEFAULT_DB_HOST};
    int dbPort{DEFAULT_DB_PORT};
    std::string dbUser{DEFAULT_DB_USER};
    std::string dbPass{DEFAULT_DB_PASS};
    std::string dbName{DEFAULT_DB_NAME};
    std::string redisHost{DEFAULT_REDIS_HOST};
    int redisPort{DEFAULT_REDIS_PORT};
    std::string zmqEndpoint;
    std::string modbusIp;  // 空=从数据库读取
    int modbusPort{0};
    int pollIntervalMs{DEFAULT_POLL_MS};
    int collectCycles{DEFAULT_CYCLES};
    std::string pluginDir;  // 自动解析
};

static AppConfig parse_args(int argc, char* argv[]) {
    AppConfig cfg;
    // 插件目录: 相对于可执行文件路径的绝对路径
    cfg.pluginDir =
        fs::absolute(fs::path(argv[0]).parent_path() / "../../lib").lexically_normal().string();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--db-host" && i + 1 < argc)
            cfg.dbHost = argv[++i];
        else if (arg == "--db-port" && i + 1 < argc)
            cfg.dbPort = std::stoi(argv[++i]);
        else if (arg == "--db-user" && i + 1 < argc)
            cfg.dbUser = argv[++i];
        else if (arg == "--db-pass" && i + 1 < argc)
            cfg.dbPass = argv[++i];
        else if (arg == "--db-name" && i + 1 < argc)
            cfg.dbName = argv[++i];
        else if (arg == "--redis-host" && i + 1 < argc)
            cfg.redisHost = argv[++i];
        else if (arg == "--redis-port" && i + 1 < argc)
            cfg.redisPort = std::stoi(argv[++i]);
        else if (arg == "--zmq-endpoint" && i + 1 < argc)
            cfg.zmqEndpoint = argv[++i];
        else if (arg == "--modbus-ip" && i + 1 < argc)
            cfg.modbusIp = argv[++i];
        else if (arg == "--modbus-port" && i + 1 < argc)
            cfg.modbusPort = std::stoi(argv[++i]);
        else if (arg == "--poll-ms" && i + 1 < argc)
            cfg.pollIntervalMs = std::stoi(argv[++i]);
        else if (arg == "--cycles" && i + 1 < argc)
            cfg.collectCycles = std::stoi(argv[++i]);
        else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        }
    }
    return cfg;
}

// ============================================================
// main
// ============================================================

int main(int argc, char* argv[]) {
    auto cfg = parse_args(argc, argv);

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "\n  ╔════════════════════════════════════════════╗\n"
              << "  ║     EMS 数据采集全流程示例                  ║\n"
              << "  ║    插件目录: " << cfg.pluginDir << "\n"
              << "  ╚════════════════════════════════════════════╝\n"
              << std::endl;

    // ============================================================
    // 1. 数据库初始化
    // ============================================================

    std::cout << "[1/4] 初始化数据库..." << std::endl;
    std::cout << "  数据库: " << cfg.dbHost << ":" << cfg.dbPort << "/" << cfg.dbName << std::endl;

    ems::db::DbConfig dbConfig;
    dbConfig.host = cfg.dbHost;
    dbConfig.port = cfg.dbPort;
    dbConfig.user = cfg.dbUser;
    dbConfig.password = cfg.dbPass;
    dbConfig.dbname = cfg.dbName;

    // 建库建表 + 写入示例数据
    std::cout << "  检查并初始化数据库和表..." << std::endl;
    if (!ems::db::ensure_database_with_seed(dbConfig)) {
        std::cout << "  ⚠ 自动建库建表失败，尝试使用已有数据库继续" << std::endl;
    } else {
        std::cout << "  ✅ 数据库和表已就绪" << std::endl;
    }

    // ============================================================
    // 2. 从数据库加载配置
    // ============================================================

    std::cout << "[2/4] 从数据库加载配置..." << std::endl;

    ParaLoader loader;
    if (!loader.initialize(dbConfig)) {
        std::cerr << "  ❌ 数据库连接失败: " << loader.last_error() << std::endl;
        return 1;
    }
    std::cout << "  ✅ 数据库连接成功" << std::endl;

    auto devEntities = loader.load_devices();
    auto chEntities = loader.load_channels();
    auto yc_points = loader.load_yc_points();
    auto yx_points = loader.load_yx_points();
    auto channelConfigs = loader.to_channel_configs(chEntities);

    std::cout << "  设备: " << devEntities.size() << "  通道: " << chEntities.size()
              << "  YC点: " << yc_points.size() << "  YX点: " << yx_points.size() << std::endl;

    if (channelConfigs.empty()) {
        std::cerr << "  ❌ 没有启用的通道，请先通过 seed 数据初始化" << std::endl;
        return 1;
    }

    // ============================================================
    // 3. 初始化采集引擎 + 启动通道
    // ============================================================

    std::cout << "[3/4] 初始化采集引擎并启动通道..." << std::endl;

    ems::net::collect::EngineConfig engineCfg;
    engineCfg.pluginDir_ = cfg.pluginDir;
    engineCfg.redisHost_ = cfg.redisHost;
    engineCfg.redisPort_ = cfg.redisPort;
    if (!cfg.zmqEndpoint.empty()) {
        engineCfg.notificationBackend_ =
            ems::net::collect::DataDispatcherConfig::NotificationBackend::ZeroMQ;
        engineCfg.zmqEndpoint_ = cfg.zmqEndpoint;
    }
    engineCfg.defaultPollIntervalMs_ = cfg.pollIntervalMs;

    g_engine = std::make_shared<ems::net::collect::CollectEngine>();
    if (!g_engine->initialize(engineCfg)) {
        std::cerr << "  ❌ 采集引擎初始化失败" << std::endl;
        return 1;
    }
    std::cout << "  ✅ 采集引擎初始化成功" << std::endl;

    g_engine->set_error_callback([](int64_t chId, const std::string& err) {
        std::cerr << "  ⚠ 通道 [" << chId << "] " << err << std::endl;
    });

    auto& pluginMgr = g_engine->plugin_manager();
    auto dispatcher = g_engine->data_dispatcher();
    int startedCount = 0;

    // 命令行 Modbus 参数覆盖数据库配置
    for (auto& chCfg : channelConfigs) {
        if (cfg.modbusIp.size() > 0) {
            std::string oldConn = chCfg.connection_;
            chCfg.connection_ =
                cfg.modbusIp + ":" + std::to_string(cfg.modbusPort ? cfg.modbusPort : 502);
            std::cout << "  📌 覆盖通道 [" << chCfg.id_ << "] 连接: " << oldConn << " → "
                      << chCfg.connection_ << std::endl;
        }
    }

    for (const auto& chCfg : channelConfigs) {
        if (chCfg.status_ == 0) {
            std::cout << "  ⏭ 通道 [" << chCfg.id_ << "] 已禁用" << std::endl;
            continue;
        }

        std::string protoName = ems::rtdb::protocolTypeToString(chCfg.protocol_);
        std::cout << "  加载协议 [" << chCfg.id_ << "]: " << protoName << std::endl;

        void* pluginInst = pluginMgr.load(protoName);
        if (!pluginInst) {
            std::cerr << "  ❌ 插件加载失败: " << pluginMgr.last_error() << std::endl;
            continue;
        }

        // 构建设备上下文
        std::map<int64_t, ems::net::collect::DeviceContext> devMap;

        for (const auto& pt : yc_points) {
            if (pt.channel_id != chCfg.id_) continue;
            auto& ctx = devMap[pt.device_id];
            ctx.deviceId_ = pt.device_id;
            ctx.deviceName_ = pt.name;
            std::string code = std::to_string(pt.slave_id) + ":" + std::to_string(pt.func_code) +
                               ":" + std::to_string(pt.reg_addr) + ":" +
                               std::to_string(pt.decode_code) + ":" + std::to_string(pt.endian);
            ctx.ycPointCodes_.push_back(code);
        }

        for (const auto& pt : yx_points) {
            if (pt.channel_id != chCfg.id_) continue;
            auto& ctx = devMap[pt.device_id];
            ctx.deviceId_ = pt.device_id;
            ctx.deviceName_ = pt.name;
            std::string code = std::to_string(pt.slave_id) + ":" + std::to_string(pt.func_code) +
                               ":" + std::to_string(pt.reg_addr) + ":" +
                               std::to_string(pt.decode_code) + ":" + std::to_string(pt.endian);
            ctx.yxPointCodes_.push_back(code);
        }

        std::vector<ems::net::collect::DeviceContext> devices;
        for (auto& [_, d] : devMap) devices.push_back(std::move(d));

        auto chThread = std::make_shared<ems::net::collect::ChannelThread>(chCfg, dispatcher);
        chThread->set_devices(std::move(devices));
        chThread->set_poll_interval(cfg.pollIntervalMs);
        chThread->set_error_callback([chId = chCfg.id_, connection = chCfg.connection_](
                                         int64_t, const std::string& err) {
            std::cerr << "  ⚠ 通道 [" << chId << "] " << connection << " - " << err << std::endl;
        });

        auto fns = pluginMgr.get_functions(protoName);
        chThread->start(pluginMgr, pluginInst, fns);
        startedCount++;

        std::cout << "  ✅ 通道 [" << chCfg.id_ << "] " << chCfg.name_
                  << " 已启动 (协议=" << protoName << ", 周期=" << cfg.pollIntervalMs << "ms)"
                  << std::endl;
    }

    // ============================================================
    // 4. 采集循环
    // ============================================================

    int startedChannels = startedCount;  // 本地追踪已启动通道数
    int cycle = 0;
    int total = (cfg.collectCycles <= 0) ? INT_MAX : cfg.collectCycles;
    std::cout << "\n[4/4] 开始采集 (按 Ctrl+C 停止)..." << std::endl;

    while (g_running && cycle < total) {
        // 分小段 sleep 使 Ctrl+C 能快速响应
        for (int i = 0; i < (cfg.pollIntervalMs + 100) / 100 && g_running; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!g_running) break;
        cycle++;

        auto stats = g_engine->data_dispatcher() ? g_engine->data_dispatcher()->stats()
                                                 : ems::net::collect::DataDispatcher::Stats{};

        std::cout << "\n  ── 采集轮次 #" << cycle << " ──\n"
                  << "  启动通道: " << startedChannels << "  更新: " << stats.totalUpdates
                  << "  失败: " << stats.failedUpdates << "  批量: " << stats.batchCount
                  << std::endl;
    }

    // ============================================================
    // 5. 清理
    // ============================================================

    std::cout << "\n  停止采集引擎..." << std::endl;

    // 先收集统计信息
    auto statsFinal = g_engine && g_engine->data_dispatcher()
                          ? g_engine->data_dispatcher()->stats()
                          : ems::net::collect::DataDispatcher::Stats{};

    if (g_engine) {
        g_engine->stop_all();
        g_engine.reset();
    }

    std::cout << "\n  ╔══════════════════════════════════════════╗\n"
              << "  ║   采集完成                              ║\n"
              << "  ║   总更新: " << std::setw(6) << statsFinal.totalUpdates
              << "  失败: " << std::setw(4) << statsFinal.failedUpdates << "   ║\n"
              << "  ║   总发布: " << std::setw(6) << statsFinal.totalPublishes
              << "  批量: " << std::setw(4) << statsFinal.batchCount << "   ║\n"
              << "  ╚══════════════════════════════════════════╝\n"
              << std::endl;

    return 0;
}

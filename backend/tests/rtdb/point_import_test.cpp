/**
 * @file point_import_test.cpp
 * @brief 点表导入单元测试（不依赖Redis/Mock，直接测试导入相关核心逻辑）
 */
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <vector>

#include "rtdb.h"
#include "rtdb_cache.h"

using namespace ems::rtdb;

// ============================================================
// 测试 Fixture：为测试提供 RtDbConfigCache 和 RtDbPointManager
// ============================================================
class PointImportTest : public ::testing::Test {
   protected:
    void SetUp() override {
        cache = std::make_shared<RtDbConfigCache>();
        pointManager = std::make_shared<RtDbPointManager>();
        pointManager->setConfigCache(cache);
    }

    void TearDown() override {
        cache.reset();
        pointManager.reset();
    }

    std::shared_ptr<RtDbConfigCache> cache;
    std::shared_ptr<RtDbPointManager> pointManager;

    // 创建临时JSON文件的辅助方法
    std::string createTempJsonFile(const std::string& content) {
        char tmpPath[] = "/tmp/ems_test_XXXXXX";
        int fd = mkstemp(tmpPath);
        if (fd == -1) {
            return "";
        }
        write(fd, content.c_str(), content.size());
        close(fd);
        return std::string(tmpPath);
    }

    // 删除临时文件
    void removeTempFile(const std::string& path) {
        if (!path.empty()) {
            std::remove(path.c_str());
        }
    }
};

// ============================================================
// 1. RtDbConfigCache 核心功能测试
// ============================================================

TEST_F(PointImportTest, LoadYcConfigs) {
    std::vector<RtDbConfigCache::YcConfig> configs;
    RtDbConfigCache::YcConfig cfg1;
    cfg1.code = "YC_001";
    cfg1.name = "母线电压";
    cfg1.channel_id = 1;
    cfg1.mulCoe = 1.0;
    cfg1.addCoe = 0.0;
    cfg1.maxLimit = 1000.0;
    cfg1.minLimit = 0.0;
    cfg1.unit = "V";
    cfg1.status = 1;
    configs.push_back(cfg1);

    RtDbConfigCache::YcConfig cfg2;
    cfg2.code = "YC_002";
    cfg2.name = "有功功率";
    cfg2.channel_id = 1;
    cfg2.mulCoe = 0.1;
    cfg2.addCoe = 0.0;
    cfg2.maxLimit = 1000.0;
    cfg2.minLimit = -1000.0;
    cfg2.unit = "MW";
    cfg2.status = 1;
    configs.push_back(cfg2);

    cache->loadYcConfigs(configs);

    EXPECT_EQ(cache->getYcConfigCount(), 2);

    RtDbConfigCache::YcConfig retrieved;
    EXPECT_TRUE(cache->getYcConfig("YC_001", retrieved));
    EXPECT_EQ(retrieved.name, "母线电压");
    EXPECT_EQ(retrieved.mulCoe, 1.0);
    EXPECT_EQ(retrieved.unit, "V");

    EXPECT_TRUE(cache->hasYcConfig("YC_001"));
    EXPECT_TRUE(cache->hasYcConfig("YC_002"));
    EXPECT_FALSE(cache->hasYcConfig("NON_EXISTENT"));

    auto codes = cache->getYcConfigCodes();
    EXPECT_EQ(codes.size(), 2);
    EXPECT_NE(std::find(codes.begin(), codes.end(), "YC_001"), codes.end());
    EXPECT_NE(std::find(codes.begin(), codes.end(), "YC_002"), codes.end());
}

TEST_F(PointImportTest, LoadYxConfigs) {
    std::vector<RtDbConfigCache::YxConfig> configs;
    RtDbConfigCache::YxConfig cfg1;
    cfg1.code = "YX_001";
    cfg1.name = "断路器状态";
    cfg1.channel_id = 1;
    cfg1.bit = 0;
    cfg1.reverse = false;
    cfg1.status = 1;
    configs.push_back(cfg1);

    RtDbConfigCache::YxConfig cfg2;
    cfg2.code = "YX_002";
    cfg2.name = "隔离开关状态";
    cfg2.channel_id = 1;
    cfg2.bit = 1;
    cfg2.reverse = true;
    cfg2.status = 1;
    configs.push_back(cfg2);

    cache->loadYxConfigs(configs);

    EXPECT_EQ(cache->getYxConfigCount(), 2);

    RtDbConfigCache::YxConfig retrieved;
    EXPECT_TRUE(cache->getYxConfig("YX_001", retrieved));
    EXPECT_EQ(retrieved.bit, 0);
    EXPECT_FALSE(retrieved.reverse);

    EXPECT_TRUE(cache->getYxConfig("YX_002", retrieved));
    EXPECT_EQ(retrieved.bit, 1);
    EXPECT_TRUE(retrieved.reverse);

    EXPECT_TRUE(cache->hasYxConfig("YX_001"));
    EXPECT_FALSE(cache->hasYxConfig("NON_EXISTENT"));

    auto codes = cache->getYxConfigCodes();
    EXPECT_EQ(codes.size(), 2);
}

TEST_F(PointImportTest, LoadYkConfigs) {
    std::vector<RtDbConfigCache::YkConfig> configs;
    RtDbConfigCache::YkConfig cfg1;
    cfg1.code = "YK_001";
    cfg1.name = "断路器遥控";
    cfg1.channel_id = 1;
    cfg1.bit = 0;
    cfg1.commandType = 0;
    cfg1.status = 1;
    configs.push_back(cfg1);

    cache->loadYkConfigs(configs);

    EXPECT_EQ(cache->getYkConfigCount(), 1);

    RtDbConfigCache::YkConfig retrieved;
    EXPECT_TRUE(cache->getYkConfig("YK_001", retrieved));
    EXPECT_EQ(retrieved.name, "断路器遥控");
    EXPECT_TRUE(cache->hasYkConfig("YK_001"));
}

TEST_F(PointImportTest, LoadYtConfigs) {
    std::vector<RtDbConfigCache::YtConfig> configs;
    RtDbConfigCache::YtConfig cfg1;
    cfg1.code = "YT_001";
    cfg1.name = "分接头调节";
    cfg1.channel_id = 1;
    cfg1.mulCoe = 0.01;
    cfg1.addCoe = 0.0;
    cfg1.maxLimit = 100.0;
    cfg1.minLimit = 0.0;
    cfg1.unit = "%";
    cfg1.status = 1;
    configs.push_back(cfg1);

    cache->loadYtConfigs(configs);

    EXPECT_EQ(cache->getYtConfigCount(), 1);

    RtDbConfigCache::YtConfig retrieved;
    EXPECT_TRUE(cache->getYtConfig("YT_001", retrieved));
    EXPECT_EQ(retrieved.mulCoe, 0.01);
    EXPECT_TRUE(cache->hasYtConfig("YT_001"));
}

TEST_F(PointImportTest, PointConfigCrud) {
    PointConfig pc;
    pc.code_ = "TEST_YC_001";
    pc.name_ = "测试遥测";
    pc.pointType_ = PointType::YC;
    pc.channelId_ = 1;
    pc.mulCoe_ = 1.0;
    pc.addCoe_ = 0.0;
    pc.maxLimit_ = 500.0;
    pc.minLimit_ = -500.0;
    pc.unit_ = "kW";
    pc.status_ = 1;

    EXPECT_TRUE(cache->setPointConfig(pc));

    PointConfig retrieved;
    EXPECT_TRUE(cache->getPointConfig("TEST_YC_001", retrieved));
    EXPECT_EQ(retrieved.name_, "测试遥测");
    EXPECT_EQ(retrieved.unit_, "kW");

    EXPECT_TRUE(cache->deletePointConfig("TEST_YC_001"));
    EXPECT_FALSE(cache->getPointConfig("TEST_YC_001", retrieved));
}

TEST_F(PointImportTest, GetAllPointConfigs) {
    PointConfig yc;
    yc.code_ = "YC_ALL_001";
    yc.name_ = "全部遥测";
    yc.pointType_ = PointType::YC;
    cache->setPointConfig(yc);

    PointConfig yx;
    yx.code_ = "YX_ALL_001";
    yx.name_ = "全部遥信";
    yx.pointType_ = PointType::YX;
    cache->setPointConfig(yx);

    PointConfig yk;
    yk.code_ = "YK_ALL_001";
    yk.name_ = "全部遥控";
    yk.pointType_ = PointType::YK;
    cache->setPointConfig(yk);

    PointConfig yt;
    yt.code_ = "YT_ALL_001";
    yt.name_ = "全部遥调";
    yt.pointType_ = PointType::YT;
    cache->setPointConfig(yt);

    auto all = cache->getAllPointConfigs();
    EXPECT_EQ(all.size(), 4);

    bool foundYc = false, foundYx = false, foundYk = false, foundYt = false;
    for (const auto& c : all) {
        if (c.code_ == "YC_ALL_001") foundYc = true;
        if (c.code_ == "YX_ALL_001") foundYx = true;
        if (c.code_ == "YK_ALL_001") foundYk = true;
        if (c.code_ == "YT_ALL_001") foundYt = true;
    }
    EXPECT_TRUE(foundYc);
    EXPECT_TRUE(foundYx);
    EXPECT_TRUE(foundYk);
    EXPECT_TRUE(foundYt);
}

TEST_F(PointImportTest, ClearConfigs) {
    RtDbConfigCache::YcConfig yc;
    yc.code = "YC_CLR_001";
    cache->loadYcConfigs({yc});

    RtDbConfigCache::YxConfig yx;
    yx.code = "YX_CLR_001";
    cache->loadYxConfigs({yx});

    EXPECT_EQ(cache->getYcConfigCount(), 1);
    EXPECT_EQ(cache->getYxConfigCount(), 1);

    cache->clearYc();
    EXPECT_EQ(cache->getYcConfigCount(), 0);
    EXPECT_EQ(cache->getYxConfigCount(), 1);

    cache->clear();
    EXPECT_EQ(cache->getYxConfigCount(), 0);
    EXPECT_EQ(cache->getYkConfigCount(), 0);
    EXPECT_EQ(cache->getYtConfigCount(), 0);
}

// ============================================================
// 2. JSON 文件导入测试
// ============================================================

TEST_F(PointImportTest, JsonImportYcPoints) {
    std::string jsonContent = R"(
{
    "yc_points": [
        {
            "code": "IMPORT_YC_001",
            "name": "导入测试电压",
            "channel_id": 1,
            "decode_code": "UINT16",
            "mul_coe": 1.0,
            "add_coe": 0.0,
            "max_limit": 1000.0,
            "min_limit": 0.0,
            "unit": "V",
            "status": 1
        },
        {
            "code": "IMPORT_YC_002",
            "name": "导入测试电流",
            "channel_id": 1,
            "decode_code": "INT16",
            "mul_coe": 0.01,
            "add_coe": 0.0,
            "max_limit": 5000.0,
            "min_limit": 0.0,
            "unit": "A",
            "status": 1
        }
    ]
}
)";

    std::vector<RtDbConfigCache::YcConfig> ycConfigs;
    std::vector<RtDbConfigCache::YxConfig> yxConfigs;
    std::vector<RtDbConfigCache::YkConfig> ykConfigs;
    std::vector<RtDbConfigCache::YtConfig> ytConfigs;

    nlohmann::json root;
    ASSERT_NO_THROW(root = nlohmann::json::parse(jsonContent));

    auto parseYcConfig = [](const nlohmann::json& j) -> RtDbConfigCache::YcConfig {
        RtDbConfigCache::YcConfig cfg;
        cfg.code = j.value("code", "");
        cfg.name = j.value("name", "");
        cfg.channel_id = j.value("channel_id", 0LL);
        cfg.mulCoe = j.value("mul_coe", 0.0);
        cfg.addCoe = j.value("add_coe", 0.0);
        cfg.maxLimit = j.value("max_limit", 0.0);
        cfg.minLimit = j.value("min_limit", 0.0);
        cfg.unit = j.value("unit", "");
        cfg.status = j.value("status", 0);
        return cfg;
    };

    if (root.contains("yc_points")) {
        for (const auto& item : root["yc_points"]) {
            ycConfigs.push_back(parseYcConfig(item));
        }
    }

    EXPECT_EQ(ycConfigs.size(), 2);
    EXPECT_EQ(yxConfigs.size(), 0);
    EXPECT_EQ(ykConfigs.size(), 0);
    EXPECT_EQ(ytConfigs.size(), 0);

    cache->loadYcConfigs(ycConfigs);

    RtDbConfigCache::YcConfig retrieved;
    EXPECT_TRUE(cache->getYcConfig("IMPORT_YC_001", retrieved));
    EXPECT_EQ(retrieved.name, "导入测试电压");
    EXPECT_EQ(retrieved.unit, "V");
    EXPECT_EQ(retrieved.mulCoe, 1.0);

    EXPECT_TRUE(cache->getYcConfig("IMPORT_YC_002", retrieved));
    EXPECT_EQ(retrieved.name, "导入测试电流");
    EXPECT_EQ(retrieved.unit, "A");
    EXPECT_EQ(retrieved.mulCoe, 0.01);
}

TEST_F(PointImportTest, JsonImportWithFile) {
    std::string jsonContent = R"(
{
    "yc_points": [
        {
            "code": "FILE_YC_001",
            "name": "文件导入测试点",
            "channel_id": 1,
            "mul_coe": 2.0,
            "add_coe": 1.0,
            "max_limit": 200.0,
            "min_limit": -200.0,
            "unit": "kW",
            "status": 1
        }
    ]
}
)";

    std::string tmpFile = createTempJsonFile(jsonContent);
    ASSERT_FALSE(tmpFile.empty());

    std::ifstream file(tmpFile);
    ASSERT_TRUE(file.is_open());
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    std::vector<RtDbConfigCache::YcConfig> ycConfigs;

    nlohmann::json root;
    ASSERT_NO_THROW(root = nlohmann::json::parse(buffer.str()));

    auto parseYcConfig = [](const nlohmann::json& j) -> RtDbConfigCache::YcConfig {
        RtDbConfigCache::YcConfig cfg;
        cfg.code = j.value("code", "");
        cfg.name = j.value("name", "");
        cfg.channel_id = j.value("channel_id", 0LL);
        cfg.mulCoe = j.value("mul_coe", 0.0);
        cfg.addCoe = j.value("add_coe", 0.0);
        cfg.maxLimit = j.value("max_limit", 0.0);
        cfg.minLimit = j.value("min_limit", 0.0);
        cfg.unit = j.value("unit", "");
        cfg.status = j.value("status", 0);
        return cfg;
    };

    if (root.contains("yc_points")) {
        for (const auto& item : root["yc_points"]) {
            ycConfigs.push_back(parseYcConfig(item));
        }
    }

    EXPECT_EQ(ycConfigs.size(), 1);
    cache->loadYcConfigs(ycConfigs);

    RtDbConfigCache::YcConfig retrieved;
    EXPECT_TRUE(cache->getYcConfig("FILE_YC_001", retrieved));
    EXPECT_EQ(retrieved.mulCoe, 2.0);
    EXPECT_EQ(retrieved.addCoe, 1.0);

    removeTempFile(tmpFile);
}

TEST_F(PointImportTest, JsonImportWithEmptyArrays) {
    std::string jsonContent = R"(
{
    "yc_points": [],
    "yx_points": [],
    "yk_points": [],
    "yt_points": []
}
)";

    std::vector<RtDbConfigCache::YcConfig> ycConfigs;

    nlohmann::json root;
    ASSERT_NO_THROW(root = nlohmann::json::parse(jsonContent));

    if (root.contains("yc_points")) {
        for (const auto& item : root["yc_points"]) {
            RtDbConfigCache::YcConfig cfg;
            cfg.code = item.value("code", "");
            ycConfigs.push_back(cfg);
        }
    }
    EXPECT_EQ(ycConfigs.size(), 0);
}

TEST_F(PointImportTest, JsonImportWithInvalidJson) {
    std::string invalidJson = R"({ this is not valid json })";

    std::vector<RtDbConfigCache::YcConfig> ycConfigs;
    EXPECT_THROW(nlohmann::json::parse(invalidJson), nlohmann::json::parse_error);

    cache->loadYcConfigs(ycConfigs);
    EXPECT_EQ(cache->getYcConfigCount(), 0);
}

// ============================================================
// 3. 点数据 JSON 序列化/反序列化测试
// ============================================================

TEST_F(PointImportTest, YcPointJsonSerialization) {
    YcPoint point;
    point.code_ = "YC_SER_001";
    point.name_ = "序列化测试点";
    point.decodeCode_ = DecodeCode::UINT16;
    point.value_ = 220.5;
    point.rawValue_ = 2205;
    point.mulCoe_ = 0.1;
    point.addCoe_ = 0.0;
    point.minLimit_ = 0.0;
    point.maxLimit_ = 1000.0;
    point.unit_ = "V";
    point.timestamp_ = 1234567890;
    point.quality_ = PointQuality::Good;
    point.status_ = 1;

    std::string json = point.toJson();
    EXPECT_FALSE(json.empty());

    YcPoint deserialized;
    deserialized.fromJson(json);

    EXPECT_EQ(deserialized.code_, "YC_SER_001");
    EXPECT_EQ(deserialized.name_, "序列化测试点");
    EXPECT_EQ(deserialized.value_, 220.5);
    EXPECT_EQ(deserialized.rawValue_, 2205);
    EXPECT_EQ(deserialized.mulCoe_, 0.1);
    EXPECT_EQ(deserialized.unit_, "V");
    EXPECT_EQ(deserialized.quality_, PointQuality::Good);
    EXPECT_EQ(deserialized.status_, 1);
}

TEST_F(PointImportTest, YxPointJsonSerialization) {
    YxPoint point;
    point.code_ = "YX_SER_001";
    point.name_ = "遥信序列化测试";
    point.decodeCode_ = DecodeCode::BOOL;
    point.value_ = 1;
    point.bit_ = 0;
    point.reverse_ = true;
    point.timestamp_ = 1234567890;
    point.quality_ = PointQuality::Good;
    point.status_ = 1;

    std::string json = point.toJson();
    EXPECT_FALSE(json.empty());

    YxPoint deserialized;
    deserialized.fromJson(json);

    EXPECT_EQ(deserialized.code_, "YX_SER_001");
    EXPECT_EQ(deserialized.name_, "遥信序列化测试");
    EXPECT_EQ(deserialized.value_, 1);
    EXPECT_EQ(deserialized.bit_, 0);
    EXPECT_TRUE(deserialized.reverse_);
    EXPECT_EQ(deserialized.quality_, PointQuality::Good);
}

TEST_F(PointImportTest, YkPointJsonSerialization) {
    YkPoint point;
    point.code_ = "YK_SER_001";
    point.name_ = "遥控序列化测试";
    point.decodeCode_ = DecodeCode::BOOL;
    point.value_ = 1;
    point.commandType_ = 0;
    point.ykStatus_ = YKStatus::Success;
    point.timestamp_ = 1234567890;
    point.executeTime_ = 1234567895;
    point.result_ = "执行成功";

    std::string json = point.toJson();
    EXPECT_FALSE(json.empty());

    YkPoint deserialized;
    deserialized.fromJson(json);

    EXPECT_EQ(deserialized.code_, "YK_SER_001");
    EXPECT_EQ(deserialized.name_, "遥控序列化测试");
    EXPECT_EQ(deserialized.value_, 1);
    EXPECT_EQ(deserialized.ykStatus_, YKStatus::Success);
    EXPECT_EQ(deserialized.result_, "执行成功");
}

TEST_F(PointImportTest, YtPointJsonSerialization) {
    YtPoint point;
    point.code_ = "YT_SER_001";
    point.name_ = "遥调序列化测试";
    point.decodeCode_ = DecodeCode::UINT16;
    point.value_ = 50.0;
    point.mulCoe_ = 1.0;
    point.addCoe_ = 0.0;
    point.minLimit_ = 0.0;
    point.maxLimit_ = 100.0;
    point.unit_ = "%";
    point.timestamp_ = 1234567890;
    point.quality_ = PointQuality::Good;
    point.status_ = 1;

    std::string json = point.toJson();
    EXPECT_FALSE(json.empty());

    YtPoint deserialized;
    deserialized.fromJson(json);

    EXPECT_EQ(deserialized.code_, "YT_SER_001");
    EXPECT_EQ(deserialized.name_, "遥调序列化测试");
    EXPECT_EQ(deserialized.value_, 50.0);
    EXPECT_EQ(deserialized.unit_, "%");
    EXPECT_EQ(deserialized.quality_, PointQuality::Good);
}

TEST_F(PointImportTest, YcPointUpdate) {
    YcPoint point1;
    point1.code_ = "YC_UPD_001";
    point1.value_ = 100.0;
    point1.rawValue_ = 1000;
    point1.timestamp_ = 100;
    point1.quality_ = PointQuality::Good;
    point1.status_ = 0;

    YcPoint point2;
    point2.code_ = "YC_UPD_001";
    point2.value_ = 200.0;
    point2.rawValue_ = 2000;
    point2.timestamp_ = 200;
    point2.quality_ = PointQuality::Error;
    point2.status_ = 1;

    point1.update(point2);

    EXPECT_EQ(point1.value_, 200.0);
    EXPECT_EQ(point1.rawValue_, 2000);
    EXPECT_EQ(point1.timestamp_, 200);
    EXPECT_EQ(point1.quality_, PointQuality::Error);
    EXPECT_EQ(point1.status_, 1);
}

// ============================================================
// 4. 点类型转换与验证测试
// ============================================================

TEST_F(PointImportTest, YcValueTransformAndValidate) {
    RtDbConfigCache::YcConfig config;
    config.code = "TRANSFORM_YC";
    config.mulCoe = 0.1;
    config.addCoe = 0.0;
    config.minLimit = -100.0;
    config.maxLimit = 500.0;
    cache->loadYcConfigs({config});

    double transformed = pointManager->transformYcValue(2200, config);
    EXPECT_DOUBLE_EQ(transformed, 220.0);

    EXPECT_TRUE(pointManager->validateYcValue(220.0, config));
    EXPECT_TRUE(pointManager->validateYcValue(-100.0, config));
    EXPECT_TRUE(pointManager->validateYcValue(500.0, config));
    EXPECT_FALSE(pointManager->validateYcValue(-101.0, config));
    EXPECT_FALSE(pointManager->validateYcValue(501.0, config));
}

TEST_F(PointImportTest, YtValueTransformAndValidate) {
    RtDbConfigCache::YtConfig config;
    config.code = "TRANSFORM_YT";
    config.mulCoe = 0.01;
    config.addCoe = 0.0;
    config.minLimit = 0.0;
    config.maxLimit = 100.0;
    cache->loadYtConfigs({config});

    double transformed = pointManager->transformYtValue(5000, config);
    EXPECT_DOUBLE_EQ(transformed, 50.0);

    EXPECT_TRUE(pointManager->validateYtValue(0.0, config));
    EXPECT_TRUE(pointManager->validateYtValue(50.0, config));
    EXPECT_TRUE(pointManager->validateYtValue(100.0, config));
    EXPECT_FALSE(pointManager->validateYtValue(-0.1, config));
    EXPECT_FALSE(pointManager->validateYtValue(100.1, config));
}

// ============================================================
// 5. PointConfig 类型转换测试
// ============================================================

TEST_F(PointImportTest, ConfigConversionFromYc) {
    PointConfig pc;
    pc.code_ = "CONV_YC_001";
    pc.name_ = "转换测试";
    pc.pointType_ = PointType::YC;
    pc.channelId_ = 2;
    pc.mulCoe_ = 2.0;
    pc.addCoe_ = 1.0;
    pc.maxLimit_ = 300.0;
    pc.minLimit_ = -300.0;
    pc.unit_ = "Hz";
    pc.status_ = 1;

    EXPECT_TRUE(cache->setPointConfig(pc));

    RtDbConfigCache::YcConfig ycConfig;
    EXPECT_TRUE(cache->getYcConfig("CONV_YC_001", ycConfig));
    EXPECT_EQ(ycConfig.name, "转换测试");
    EXPECT_EQ(ycConfig.channel_id, 2);
    EXPECT_EQ(ycConfig.mulCoe, 2.0);
    EXPECT_EQ(ycConfig.addCoe, 1.0);
    EXPECT_EQ(ycConfig.unit, "Hz");
}

TEST_F(PointImportTest, ConfigConversionFromYx) {
    PointConfig pc;
    pc.code_ = "CONV_YX_001";
    pc.name_ = "遥信转换";
    pc.pointType_ = PointType::YX;
    pc.channelId_ = 1;
    pc.bit_ = 2;
    pc.reverse_ = true;
    pc.status_ = 1;

    EXPECT_TRUE(cache->setPointConfig(pc));

    RtDbConfigCache::YxConfig yxConfig;
    EXPECT_TRUE(cache->getYxConfig("CONV_YX_001", yxConfig));
    EXPECT_EQ(yxConfig.bit, 2);
    EXPECT_TRUE(yxConfig.reverse);
}

TEST_F(PointImportTest, ConfigConversionFromYk) {
    PointConfig pc;
    pc.code_ = "CONV_YK_001";
    pc.name_ = "遥控转换";
    pc.pointType_ = PointType::YK;
    pc.channelId_ = 1;
    pc.bit_ = 3;
    pc.commandType_ = 1;
    pc.status_ = 1;

    EXPECT_TRUE(cache->setPointConfig(pc));

    RtDbConfigCache::YkConfig ykConfig;
    EXPECT_TRUE(cache->getYkConfig("CONV_YK_001", ykConfig));
    EXPECT_EQ(ykConfig.bit, 3);
    EXPECT_EQ(ykConfig.commandType, 1);
}

TEST_F(PointImportTest, ConfigConversionFromYt) {
    PointConfig pc;
    pc.code_ = "CONV_YT_001";
    pc.name_ = "遥调转换";
    pc.pointType_ = PointType::YT;
    pc.channelId_ = 1;
    pc.mulCoe_ = 0.5;
    pc.addCoe_ = 10.0;
    pc.maxLimit_ = 200.0;
    pc.minLimit_ = -200.0;
    pc.unit_ = "°C";
    pc.status_ = 1;

    EXPECT_TRUE(cache->setPointConfig(pc));

    PointConfig retrieved;
    EXPECT_TRUE(cache->getPointConfig("CONV_YT_001", retrieved));
    EXPECT_EQ(retrieved.pointType_, PointType::YT);
    EXPECT_EQ(retrieved.mulCoe_, 0.5);
    EXPECT_EQ(retrieved.addCoe_, 10.0);
    EXPECT_EQ(retrieved.unit_, "°C");
}

// ============================================================
// 6. 完整导入流程测试（模拟从JSON解析到配置加载）
// ============================================================

TEST_F(PointImportTest, FullImportWorkflow) {
    std::string jsonContent = R"(
{
    "yc_points": [
        {
            "code": "FULL_YC_001",
            "name": "全流程遥测",
            "channel_id": 1,
            "decode_code": "UINT16",
            "mul_coe": 1.0,
            "add_coe": 0.0,
            "max_limit": 10000.0,
            "min_limit": -10000.0,
            "unit": "W",
            "status": 1
        }
    ],
    "yx_points": [
        {
            "code": "FULL_YX_001",
            "name": "全流程遥信",
            "channel_id": 1,
            "decode_code": "BOOL",
            "bit": 0,
            "reverse": false,
            "status": 1
        }
    ],
    "yk_points": [],
    "yt_points": []
}
)";

    std::vector<RtDbConfigCache::YcConfig> ycConfigs;
    std::vector<RtDbConfigCache::YxConfig> yxConfigs;
    std::vector<RtDbConfigCache::YkConfig> ykConfigs;
    std::vector<RtDbConfigCache::YtConfig> ytConfigs;

    nlohmann::json root;
    ASSERT_NO_THROW(root = nlohmann::json::parse(jsonContent));

    auto parseYcConfig = [](const nlohmann::json& j) -> RtDbConfigCache::YcConfig {
        RtDbConfigCache::YcConfig cfg;
        cfg.code = j.value("code", "");
        cfg.name = j.value("name", "");
        cfg.channel_id = j.value("channel_id", 0LL);
        cfg.mulCoe = j.value("mul_coe", 0.0);
        cfg.addCoe = j.value("add_coe", 0.0);
        cfg.maxLimit = j.value("max_limit", 0.0);
        cfg.minLimit = j.value("min_limit", 0.0);
        cfg.unit = j.value("unit", "");
        cfg.status = j.value("status", 0);
        return cfg;
    };

    auto parseYxConfig = [](const nlohmann::json& j) -> RtDbConfigCache::YxConfig {
        RtDbConfigCache::YxConfig cfg;
        cfg.code = j.value("code", "");
        cfg.name = j.value("name", "");
        cfg.channel_id = j.value("channel_id", 0LL);
        cfg.bit = j.value("bit", 0);
        cfg.reverse = j.value("reverse", false);
        cfg.status = j.value("status", 0);
        return cfg;
    };

    if (root.contains("yc_points")) {
        for (const auto& item : root["yc_points"]) {
            ycConfigs.push_back(parseYcConfig(item));
        }
    }
    if (root.contains("yx_points")) {
        for (const auto& item : root["yx_points"]) {
            yxConfigs.push_back(parseYxConfig(item));
        }
    }

    EXPECT_EQ(ycConfigs.size(), 1);
    EXPECT_EQ(yxConfigs.size(), 1);

    cache->loadYcConfigs(ycConfigs);
    cache->loadYxConfigs(yxConfigs);
    cache->loadYkConfigs(ykConfigs);
    cache->loadYtConfigs(ytConfigs);

    EXPECT_EQ(cache->getYcConfigCount(), 1);
    EXPECT_EQ(cache->getYxConfigCount(), 1);
    EXPECT_EQ(cache->getYkConfigCount(), 0);
    EXPECT_EQ(cache->getYtConfigCount(), 0);

    auto allConfigs = cache->getAllPointConfigs();
    EXPECT_EQ(allConfigs.size(), 2);

    YcPoint dataPoint;
    dataPoint.code_ = "FULL_YC_001";
    dataPoint.rawValue_ = 1500;
    dataPoint.value_ = pointManager->transformYcValue(1500, ycConfigs[0]);

    EXPECT_DOUBLE_EQ(dataPoint.value_, 1500.0);
    EXPECT_TRUE(pointManager->validateYcValue(dataPoint.value_, ycConfigs[0]));
}

// ============================================================
// 7. 实时数据库（Redis）写入/读取测试
// ============================================================

class PointImportRedisTest : public ::testing::Test {
   protected:
    void SetUp() override {
        RtDbConfig config;
        config.host = "localhost";
        config.port = 6379;
        config.password = "888888";
        config.db = 0;
        config.poolSize = 10;
        config.timeoutMs = 5000;
        config.prefix = "ems:";

        rtdb = std::make_shared<RtDb>();
        if (!rtdb->initialize(config)) {
            rtdb.reset();
            GTEST_SKIP() << "无法连接到Redis服务器，跳过实时数据库测试";
        }
    }

    void TearDown() override {
        // if (rtdb) {
        //     rtdb->deleteYc("REDIS_YC_001");
        //     rtdb->deleteYx("REDIS_YX_001");
        //     rtdb->deleteYk("REDIS_YK_001");
        //     rtdb->deleteYt("REDIS_YT_001");
        //     rtdb->deleteYc("REDIS_YC_BATCH_001");
        //     rtdb->deleteYc("REDIS_YC_BATCH_002");
        // }
        rtdb.reset();
    }

    std::shared_ptr<RtDb> rtdb;
};

TEST_F(PointImportRedisTest, WriteAndReadYcPoint) {
    YcPoint point;
    point.code_ = "REDIS_YC_001";
    point.value_ = 220.5;
    point.rawValue_ = 2205;
    point.mulCoe_ = 1.0;
    point.addCoe_ = 0.0;
    point.minLimit_ = 0.0;
    point.maxLimit_ = 1000.0;
    point.unit_ = "V";
    point.timestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    point.quality_ = PointQuality::Good;
    point.status_ = 1;

    EXPECT_TRUE(rtdb->setYc("REDIS_YC_001", point));

    YcPoint retrieved;
    EXPECT_TRUE(rtdb->getYc("REDIS_YC_001", retrieved));
    EXPECT_EQ(retrieved.code_, "REDIS_YC_001");
    EXPECT_DOUBLE_EQ(retrieved.value_, 220.5);
    EXPECT_EQ(retrieved.rawValue_, 2205);
    EXPECT_EQ(retrieved.quality_, PointQuality::Good);
    EXPECT_EQ(retrieved.status_, 1);
    EXPECT_EQ(retrieved.unit_, "V");
}

TEST_F(PointImportRedisTest, WriteAndReadYxPoint) {
    YxPoint point;
    point.code_ = "REDIS_YX_001";
    point.value_ = 1;
    point.bit_ = 2;
    point.reverse_ = true;
    point.timestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    point.quality_ = PointQuality::Good;
    point.status_ = 1;

    EXPECT_TRUE(rtdb->setYx("REDIS_YX_001", point));

    YxPoint retrieved;
    EXPECT_TRUE(rtdb->getYx("REDIS_YX_001", retrieved));
    EXPECT_EQ(retrieved.code_, "REDIS_YX_001");
    EXPECT_EQ(retrieved.value_, 1);
    EXPECT_EQ(retrieved.reverse_, true);
    EXPECT_EQ(retrieved.quality_, PointQuality::Good);
}

TEST_F(PointImportRedisTest, WriteAndReadYkPoint) {
    YkPoint point;
    point.code_ = "REDIS_YK_001";
    point.value_ = 1;
    point.commandType_ = 0;
    point.ykStatus_ = YKStatus::Pending;
    point.timestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    point.executeTime_ = point.timestamp_ + 1000;
    point.result_ = "";

    EXPECT_TRUE(rtdb->setYk("REDIS_YK_001", point));

    YkPoint retrieved;
    EXPECT_TRUE(rtdb->getYk("REDIS_YK_001", retrieved));
    EXPECT_EQ(retrieved.code_, "REDIS_YK_001");
    EXPECT_EQ(retrieved.value_, 1);
    EXPECT_EQ(retrieved.commandType_, 0);
    EXPECT_EQ(retrieved.ykStatus_, YKStatus::Pending);
}

TEST_F(PointImportRedisTest, WriteAndReadYtPoint) {
    YtPoint point;
    point.code_ = "REDIS_YT_001";
    point.value_ = 50.0;
    point.mulCoe_ = 0.01;
    point.addCoe_ = 0.0;
    point.minLimit_ = 0.0;
    point.maxLimit_ = 100.0;
    point.unit_ = "%";
    point.timestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    point.quality_ = PointQuality::Good;
    point.status_ = 1;

    EXPECT_TRUE(rtdb->setYt("REDIS_YT_001", point));

    YtPoint retrieved;
    EXPECT_TRUE(rtdb->getYt("REDIS_YT_001", retrieved));
    EXPECT_EQ(retrieved.code_, "REDIS_YT_001");
    EXPECT_DOUBLE_EQ(retrieved.value_, 50.0);
    EXPECT_EQ(retrieved.unit_, "%");
    EXPECT_EQ(retrieved.quality_, PointQuality::Good);
}

TEST_F(PointImportRedisTest, BatchWriteAndReadYcPoints) {
    std::map<std::string, YcPoint> batchData;

    YcPoint p1;
    p1.code_ = "REDIS_YC_BATCH_001";
    p1.value_ = 100.0;
    p1.rawValue_ = 1000;
    p1.timestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    p1.quality_ = PointQuality::Good;
    p1.status_ = 1;
    batchData["REDIS_YC_BATCH_001"] = p1;

    YcPoint p2;
    p2.code_ = "REDIS_YC_BATCH_002";
    p2.value_ = 200.0;
    p2.rawValue_ = 2000;
    p2.timestamp_ = p1.timestamp_;
    p2.quality_ = PointQuality::Error;
    p2.status_ = 0;
    batchData["REDIS_YC_BATCH_002"] = p2;

    EXPECT_TRUE(rtdb->batchSetYc(batchData));

    std::vector<YcPoint> retrieved;
    EXPECT_TRUE(rtdb->batchGetYc({"REDIS_YC_BATCH_001", "REDIS_YC_BATCH_002"}, retrieved));
    EXPECT_EQ(retrieved.size(), 2);

    for (const auto& p : retrieved) {
        if (p.code_ == "REDIS_YC_BATCH_001") {
            EXPECT_DOUBLE_EQ(p.value_, 100.0);
            EXPECT_EQ(p.quality_, PointQuality::Good);
        } else if (p.code_ == "REDIS_YC_BATCH_002") {
            EXPECT_DOUBLE_EQ(p.value_, 200.0);
            EXPECT_EQ(p.quality_, PointQuality::Error);
        }
    }
}

TEST_F(PointImportRedisTest, DeleteYcPoint) {
    YcPoint point;
    point.code_ = "REDIS_YC_001";
    point.value_ = 100.0;
    point.timestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    point.quality_ = PointQuality::Good;

    EXPECT_TRUE(rtdb->setYc("REDIS_YC_001", point));

    YcPoint retrieved;
    EXPECT_TRUE(rtdb->getYc("REDIS_YC_001", retrieved));

    EXPECT_TRUE(rtdb->deleteYc("REDIS_YC_001"));
    EXPECT_FALSE(rtdb->getYc("REDIS_YC_001", retrieved));
}

TEST_F(PointImportRedisTest, GetYcCodes) {
    YcPoint point;
    point.code_ = "REDIS_YC_001";
    point.value_ = 100.0;
    point.timestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    point.quality_ = PointQuality::Good;
    rtdb->setYc("REDIS_YC_001", point);

    auto codes = rtdb->getYcCodes("REDIS_YC_*");
    EXPECT_GE(codes.size(), 1);
    EXPECT_NE(std::find(codes.begin(), codes.end(), "REDIS_YC_001"), codes.end());
}
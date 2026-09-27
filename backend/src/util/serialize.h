#pragma once
// 检查是否已经定义
#ifndef CEREAL_RAPIDJSON_NAMESPACE
#define CEREAL_RAPIDJSON_NAMESPACE cereal_rapidjson  // 避免和rapidjson冲突
#endif
#include <fstream>
#include <type_traits>

#include "cereal/archives/binary.hpp"
#include "cereal/archives/json.hpp"
#include "cereal/cereal.hpp"
#include "cereal/types/functional.hpp"
#include "cereal/types/map.hpp"
#include "cereal/types/memory.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/unordered_map.hpp"
#include "cereal/types/utility.hpp"
#include "cereal/types/variant.hpp"
#include "cereal/types/vector.hpp"

enum class SerializeType {
    XML,
    BINARY,
    JSON,
};

#ifndef DEFAULT_SERIALIZE_TYPE
#define DEFAULT_SERIALIZE_TYPE
static SerializeType gDefaultSerializeType = SerializeType::JSON;

/**
 * @brief 返回进程默认的序列化格式。
 * @return 得到的序列化类型。
 */
inline SerializeType getDefaultSerializeType() { return gDefaultSerializeType; }

/**
 * @brief 设置进程默认的序列化格式。
 * @param[in] type 连接或数据类型。
 */
inline void setDefaultSerializeType(SerializeType type) { gDefaultSerializeType = type; }
#endif

class ISerializable {
   public:
    /**
     * @brief 返回对象序列化文件所在目录。
     * @return 对应的文本内容。
     */
    virtual std::string serializeFileDir() const = 0;

    /**
     * @brief 返回对象在归档中的节点名称。
     * @return 对应的文本内容。
     */
    virtual std::string serializeNodeName() const = 0;
};

/** @brief 为普通对象选择默认文件名和节点名。 */
template <typename T, typename = void>
struct Serializer {
    /**
     * @brief 保留调用方提供的文件名。
     * @param[in] obj 待处理对象。
     * @param[in] filename 文件路径。
     * @return 对应的文本内容。
     */
    static std::string call_serialize_methods(const T& obj, std::string& filename) {
        return filename;
    }

    /**
     * @brief 普通对象没有自定义节点名。
     * @param[in] obj 待处理对象。
     * @return 对应的文本内容。
     */
    static std::string call_serializeNodeName(const T& obj) { return ""; }
};

/** @brief 从 ISerializable 对象取得文件目录和节点名称。 */
template <typename T>
struct Serializer<T, std::enable_if_t<std::is_base_of_v<ISerializable, T>>> {
    /**
     * @brief 按对象约定组合序列化目录和节点名。
     * @param[in] obj 待处理对象。
     * @param[in] filename 文件路径。
     * @return 对应的文本内容。
     */
    static std::string call_serialize_methods(const T& obj, std::string& filename) {
        return obj.serializeFileDir() + obj.serializeNodeName();
    }

    /**
     * @brief 返回对象声明的归档节点名。
     * @param[in] obj 待处理对象。
     * @return 对应的文本内容。
     */
    static std::string call_serializeNodeName(const T& obj) { return obj.serializeNodeName(); }
};

/**
 * @brief 按指定归档格式将对象写入文件。
 * @param[in] obj 待处理对象。
 * @param[in] type 连接或数据类型。
 * @param[in] filename 文件路径。
 * @return true 表示操作成功；false 表示失败。
 */
template <typename Obj>
bool serializeToFile(const Obj& obj, SerializeType type = getDefaultSerializeType(),
                     std::string filename = "") {
    filename = Serializer<Obj>::call_serialize_methods(obj, filename);
    std::string nodeName = Serializer<Obj>::call_serializeNodeName(obj);

    bool baseIserializable = false;
    if (std::is_base_of<ISerializable, Obj>::value) {
        baseIserializable = true;
    }

    std::ofstream os(filename);

    switch (type) {
        case SerializeType::BINARY: {
            cereal::BinaryOutputArchive archive(os);
            if (baseIserializable) {
                archive(cereal::make_nvp(nodeName, obj));
            } else {
                archive(CEREAL_NVP(obj));
            }
            break;
        }
        case SerializeType::JSON: {
            cereal::JSONOutputArchive archive(os);
            if (baseIserializable) {
                archive(cereal::make_nvp(nodeName, obj));
            } else {
                archive(CEREAL_NVP(obj));
            }
            break;
        }
        case SerializeType::XML:
        default:
            throw std::runtime_error("Unknown SerializeType");
            break;
    }
    return true;
}

/**
 * @brief 按指定归档格式从文件恢复对象。
 * @param[in,out] obj 待处理对象。
 * @param[in] type 连接或数据类型。
 * @param[in] filename 文件路径。
 * @return true 表示操作成功；false 表示失败。
 */
template <typename Obj>
bool deserializeFromFile(Obj& obj, SerializeType type = getDefaultSerializeType(),
                         std::string filename = "") {
    filename = Serializer<Obj>::call_serialize_methods(obj, filename);
    std::string nodeName = Serializer<Obj>::call_serializeNodeName(obj);

    bool baseIserializable = false;
    if (std::is_base_of<ISerializable, Obj>::value) {
        baseIserializable = true;
    }

    std::ifstream is(filename);

    switch (type) {
        case SerializeType::BINARY: {
            cereal::BinaryInputArchive archive(is);
            if (baseIserializable) {
                archive(cereal::make_nvp(nodeName, obj));
            } else {
                archive(CEREAL_NVP(obj));
            }
            break;
        }
        case SerializeType::JSON: {
            cereal::JSONInputArchive archive(is);
            if (baseIserializable) {
                archive(cereal::make_nvp(nodeName, obj));
            } else {
                archive(CEREAL_NVP(obj));
            }
            break;
        }
        case SerializeType::XML:
        default:
            throw std::runtime_error("Unknown SerializeType");
            break;
    }
    return true;
}

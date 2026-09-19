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
inline SerializeType getDefaultSerializeType() { return gDefaultSerializeType; }
inline void setDefaultSerializeType(SerializeType type) { gDefaultSerializeType = type; }
#endif

class ISerializable {
   public:
    virtual std::string serializeFileDir() const = 0;
    virtual std::string serializeNodeName() const = 0;
};

// 通用模板
template <typename T, typename = void>
struct Serializer {
    static std::string call_serialize_methods(const T& obj, std::string& filename) {
        return filename;
    }
    static std::string call_serializeNodeName(const T& obj) { return ""; }
};

// 对于ISerializable的派生类的特化版本
template <typename T>
struct Serializer<T, std::enable_if_t<std::is_base_of_v<ISerializable, T>>> {
    static std::string call_serialize_methods(const T& obj, std::string& filename) {
        return obj.serializeFileDir() + obj.serializeNodeName();
    }
    static std::string call_serializeNodeName(const T& obj) { return obj.serializeNodeName(); }
};

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
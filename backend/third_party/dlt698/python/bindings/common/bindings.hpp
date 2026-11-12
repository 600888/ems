
/** @file bindings.hpp
 * @brief Python 边界的结果转换、结构绑定与严格输入校验。
 */
#pragma once
#include <pybind11/chrono.h>
#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cmath>
#include <dlt698/codec/record_codec.hpp>
#include <dlt698/protocol/apdu/apdu.hpp>
#include <limits>
#include <map>

#include "bytes_caster.hpp"

namespace dlt698::python {
namespace py = pybind11;

/** @brief 将完整原生错误转换为 Python 分类异常。
 * @param[in] error 保留原码、偏移与上下文的错误。
 * @throws py::error_already_set 始终抛出已设置的 Python 异常。
 */
[[noreturn]] void raise_error(const Error& error);

/** @brief 将核心 UTF-8 或 Windows 系统错误文本转换为 Python Unicode。
 * @param[in] value 原始拥有型错误字节，优先按 UTF-8 解码。
 * @return 可显示的 Unicode；Windows 非 UTF-8 文本使用系统代码页。
 * @note 原字节由 Error.context_bytes 另行保留，不丢失诊断证据。
 */
py::str error_text(const std::string& value);

/** @brief 取出拥有型成功结果，失败保留完整诊断。
 * @tparam T 原生返回类型。
 * @param[in] result 被移出的原生结果。
 * @return 拥有内存的成功值。
 * @throws py::error_already_set 原生结果失败。
 */
template <class T>
T unwrap(Result<T> result) {
    if (!result) raise_error(result.error());
    return std::move(result).value();
}

/** @brief 检查无返回值操作结果。
 * @param[in] result 原生结果。
 * @throws py::error_already_set 操作失败。
 */
inline void unwrap(Result<void> result) {
    if (!result) raise_error(result.error());
}

/** @brief 只接受精确 Python 整数，拒绝布尔和超范围值。
 * @tparam T 有符号或无符号整型。
 * @param[in] value Python 输入。
 * @return 已验证的原生整数。
 * @throws py::type_error 类型不符；std::overflow_error 数值超范围。
 */
template <class T>
T integer(py::handle value) {
    if (!PyLong_Check(value.ptr()) || PyBool_Check(value.ptr()))
        throw py::type_error("expected int, not bool");
    try {
        return py::cast<T>(value);
    } catch (const py::cast_error&) {
        throw std::overflow_error("integer out of range");
    }
}

/** @brief 将连续单字节缓冲区复制成拥有型字节数组。
 * @param[in] value bytes、bytearray 或连续单字节 memoryview。
 * @return 不借用 Python 内存的副本。
 * @throws py::type_error 不支持的输入或非连续/非单字节缓冲区。
 */
inline Bytes bytes_from(py::handle value) {
    if (!PyObject_CheckBuffer(value.ptr())) throw py::type_error("expected bytes-like buffer");
    auto buffer = py::reinterpret_borrow<py::buffer>(value).request();
    if (buffer.ndim != 1 || buffer.itemsize != 1 || buffer.strides[0] != 1)
        throw py::type_error("expected contiguous one-dimensional byte buffer");
    auto first = static_cast<const std::uint8_t*>(buffer.ptr);
    return buffer.size ? Bytes(first, first + buffer.size) : Bytes{};
}

/** @brief 复制原生字节为不可变 Python bytes。
 * @param[in] value 原生拥有型字节。
 * @return 不依赖原生缓冲区生命周期的副本。
 */
inline py::bytes bytes_to(const Bytes& value) {
    return py::bytes(reinterpret_cast<const char*>(value.data()), value.size());
}

/** @brief 将有限非负秒转换为毫秒，拒绝溢出和小于毫秒的精度损失。
 * @param[in] seconds Python 配置的秒数。
 * @return 原生整数毫秒。
 * @throws py::value_error 负数、非有限数、超范围或非毫秒精度。
 */
inline std::chrono::milliseconds milliseconds(double seconds) {
    auto ms = seconds * 1000.0;
    if (!std::isfinite(ms) || ms < 0 || ms >= static_cast<double>(INT64_MAX) ||
        std::abs(ms - std::round(ms)) > 1e-7)
        throw py::value_error("timeout must be finite, nonnegative and have millisecond precision");
    return std::chrono::milliseconds(static_cast<std::int64_t>(std::round(ms)));
}

/** @brief 为聚合配置/消息注册关键字构造器，字段值保持原生默认值。
 * @tparam T 已公开的原生聚合类型。
 */
template <class T>
class Struct {
   public:
    /** @brief 创建配置类的 Python 类型。
     * @param[in] module 所属模块。
     * @param[in] name Python 类型名称。
     */
    Struct(py::module_& module, const char* name) : cls(module, name) {}

    /** @brief 注册拥有型普通字段，整数和字节额外验证。
     * @tparam V 字段类型。
     * @param[in] name 字段名称。
     * @param[in] member 原生成员指针。
     * @return 当前注册器，供链式调用。
     */
    template <class V>
    Struct& field(const char* name, V T::* member) {
        auto setter = [member](T& object, py::handle value) {
            if constexpr (std::is_integral_v<V> && !std::is_same_v<V, bool>)
                object.*member = integer<V>(value);
            else if constexpr (std::is_same_v<V, bool>) {
                if (!PyBool_Check(value.ptr())) throw py::type_error("expected bool");
                object.*member = py::cast<bool>(value);
            } else if constexpr (std::is_same_v<V, Bytes>)
                object.*member = bytes_from(value);
            else if constexpr (std::is_same_v<V, std::chrono::milliseconds>)
                object.*member = milliseconds(py::cast<double>(value));
            else
                object.*member = py::cast<V>(value);
        };
        setters[name] = setter;
        // 保留静态返回类型，让原生签名和 .pyi 能准确描述字段，而不是全部退化成 Any。
        if constexpr (std::is_same_v<V, Bytes>)
            cls.def_property(
                name, [member](const T& object) { return bytes_to(object.*member); }, setter);
        else if constexpr (std::is_same_v<V, std::chrono::milliseconds>)
            cls.def_property(
                name, [member](const T& object) { return (object.*member).count() / 1000.0; },
                setter);
        else
            cls.def_property(
                name, [member](const T& object) -> V { return object.*member; }, setter);
        return *this;
    }

    /** @brief 完成构造器；未知字段直接报错，避免配置拼写错误被忽略。 */
    void finish() {
        cls.def(py::init([fields = setters](py::kwargs kwargs) {
            T object{};
            for (auto item : kwargs) {
                auto name = py::cast<std::string>(item.first);
                auto it = fields.find(name);
                if (it == fields.end()) throw py::type_error("unknown field: " + name);
                it->second(object, item.second);
            }
            return object;
        }));
    }

    /** @brief 注册需要结果适配的回调属性，同时支持关键字构造。
     * @tparam Getter 属性读取器类型。
     * @tparam Setter 属性写入器类型。
     * @param[in] name 属性名称。
     * @param[in] getter 返回 Python 可调用对象的读取器。
     * @param[in] setter 校验并保存回调的写入器。
     * @return 当前注册器。
     */
    template <class Getter, class Setter>
    Struct& property(const char* name, Getter getter, Setter setter) {
        setters[name] = setter;
        cls.def_property(name, std::move(getter), std::move(setter));
        return *this;
    }

    py::class_<T> cls;

   private:
    std::map<std::string, std::function<void(T&, py::handle)>> setters;
};

/** @brief 注册原生错误信息和全部错误码。
 * @param[in,out] module 扩展模块。
 */
void bind_errors(py::module_& module);
/** @brief 先注册应用、传输和目录枚举，使后续字段签名可以解析。
 * @param[in,out] module 扩展模块。
 */
void bind_enums(py::module_& module);
/** @brief 先注册目录枚举。
 * @param[in,out] module 扩展模块。
 */
void bind_standard_enums(py::module_& module);
/** @brief 先注册安全后端基类，使配置字段保留精确类型。
 * @param[in,out] module 扩展模块。
 */
void bind_security_type(py::module_& module);
/** @brief 注册精确 Data 及基础描述符。
 * @param[in,out] module 扩展模块。
 */
void bind_model(py::module_& module);
/** @brief 注册记录选择器的全部分支。
 * @param[in,out] module 扩展模块。
 */
void bind_records(py::module_& module);
/** @brief 注册 APDU、配置与结果值对象。
 * @param[in,out] module 扩展模块。
 */
void bind_messages(py::module_& module);
/** @brief 注册连接服务的值类型。
 * @param[in,out] module 扩展模块。
 */
void bind_connection(py::module_& module);
/** @brief 注册写入、方法和 ThenGet 消息。
 * @param[in,out] module 扩展模块。
 */
void bind_mutation(py::module_& module);
/** @brief 注册 REPORT、PROXY 和 SECURITY 消息。
 * @param[in,out] module 扩展模块。
 */
void bind_advanced(py::module_& module);
/** @brief 注册应用、通道及设备配置。
 * @param[in,out] module 扩展模块。
 */
void bind_options(py::module_& module);
/** @brief 注册标准目录和记录查询。
 * @param[in,out] module 扩展模块。
 */
void bind_standard(py::module_& module);
/** @brief 注册复用原生实现的编解码入口。
 * @param[in,out] module 扩展模块。
 */
void bind_codec(py::module_& module);
/** @brief 注册应用入口和共享设备。
 * @param[in,out] module 扩展模块。
 */
void bind_app(py::module_& module);
/** @brief 注册显式驱动的专家/异步接口。
 * @param[in,out] module 扩展模块。
 */
void bind_expert(py::module_& module);
/** @brief 注册独立分帧、重组与 GET 分块工具。
 * @param[in,out] module 扩展模块。
 */
void bind_fragments(py::module_& module);
/** @brief 注册调用线程驱动的执行器、通道和会话组合接口。
 * @param[in,out] module 扩展模块。
 */
void bind_channels(py::module_& module);
}  // namespace dlt698::python

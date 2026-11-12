/** @file bytes_caster.hpp
 * @brief 包括 variant 内的原生 Bytes 也使用拥有型 Python bytes。
 */
#pragma once
#include <pybind11/pybind11.h>

#include <dlt698/common/bytes.hpp>

namespace pybind11::detail {
template <>
struct type_caster<dlt698::Bytes> {
    PYBIND11_TYPE_CASTER(dlt698::Bytes, const_name("bytes"));

    /** @brief 接受连续一维单字节缓冲区并复制，不接受字符串或整数列表。
     * @param[in] source Python 缓冲区。
     * @param[in] convert 是否允许转换，本转换不借用内存。
     * @return 输入类型符合要求时为 true，否则为 false。
     */
    bool load(handle source, bool /*convert*/) {
        if (!PyObject_CheckBuffer(source.ptr())) return false;
        Py_buffer buffer{};
        if (PyObject_GetBuffer(source.ptr(), &buffer, PyBUF_STRIDES) != 0) {
            PyErr_Clear();
            return false;
        }
        const bool valid =
            buffer.ndim == 1 && buffer.itemsize == 1 && PyBuffer_IsContiguous(&buffer, 'C');
        if (valid && buffer.len) {
            auto first = static_cast<const std::uint8_t*>(buffer.buf);
            value.assign(first, first + buffer.len);
        }
        PyBuffer_Release(&buffer);
        return valid;
    }

    /** @brief 复制任意嵌套位置的原生字节为 Python bytes。
     * @param[in] source 拥有型字节。
     * @param[in] policy 不保留原生内存的返回策略。
     * @param[in] parent 不使用的父对象。
     * @return 新 Python bytes 引用。
     */
    static handle cast(const dlt698::Bytes& source, return_value_policy /*policy*/,
                       handle /*parent*/) {
        return PyBytes_FromStringAndSize(reinterpret_cast<const char*>(source.data()),
                                         source.size());
    }
};
}  // namespace pybind11::detail

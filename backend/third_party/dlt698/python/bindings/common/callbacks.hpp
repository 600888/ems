/** @file callbacks.hpp
 * @brief Python 回调的拥有型结果转换；成功值或 Error，不借用原生栈。
 */
#pragma once
#include <dlt698/common/executor.hpp>

#include "bindings.hpp"

namespace dlt698::python {
/** @brief 将有限非负秒转换为执行器时钟单位，允许串口位时间的小数毫秒。
 * @param[in] seconds 延时、推进量或单调时刻，须能用原生时钟表示。
 * @return 按最近原生时钟刻度取整的时长。
 * @throws py::value_error 负数、非有限数或超出原生时钟范围。
 * @note 不先转换为毫秒，避免串口静默间隔精度丢失及毫秒到纳秒整数溢出。
 */
inline IExecutor::Clock::duration clock_duration(double seconds) {
    using Duration = IExecutor::Clock::duration;
    const double ticks = std::round(
        std::chrono::duration<double, Duration::period>(std::chrono::duration<double>(seconds))
            .count());
    if (!std::isfinite(seconds) || seconds < 0 || !std::isfinite(ticks) ||
        ticks >= static_cast<double>(std::numeric_limits<Duration::rep>::max()))
        throw py::value_error("executor time must be finite, nonnegative and representable");
    return Duration(static_cast<Duration::rep>(ticks));
}

/** @brief 创建原生到 Python 的完成通知。
 * @tparam T 原生结果值类型，允许 void。
 * @param[in] callback 非空 Python 回调；持有直到完成或取消。
 * @return 在调用线程持有 GIL 执行的原生回调；失败传 Error，void 成功传 None。
 * @throws py::type_error 回调不可调用。
 */
template <class T>
std::function<void(Result<T>)> result_callback(py::handle callback) {
    auto call = py::cast<std::function<void(py::object)>>(callback);
    if (!call) throw py::type_error("completion callback is required");
    return [call = std::move(call)](Result<T> result) {
        py::gil_scoped_acquire acquire;
        if (!result)
            call(py::cast(Error(result.error())));
        else if constexpr (std::is_void_v<T>)
            call(py::none());
        else
            call(py::cast(std::move(result).value()));
    };
}

/** @brief 将 Python 完成函数包装成一次性原生结果交付。
 * @tparam T 原生成功值类型，允许 void。
 * @param[in] handler 由原生会话或通道持有的完成处理器。
 * @return Python 可调用对象；重复完成报错，完成后释放原生处理器。
 * @note void 成功使用 None，失败使用 Error；非 void 直接使用拥有型成功值。
 */
template <class T>
py::cpp_function completion_function(std::function<void(Result<T>)> handler) {
    auto shared = std::make_shared<std::function<void(Result<T>)>>(std::move(handler));
    return py::cpp_function([shared](py::object value) {
        if (!*shared) throw py::value_error("completion already delivered");
        // 先转换再取走回调，错误参数允许修正后重试；回调重入也不能二次完成。
        Result<T> result = [&]() -> Result<T> {
            if (py::isinstance<Error>(value)) return py::cast<Error>(value);
            if constexpr (std::is_void_v<T>) {
                if (!value.is_none()) throw py::type_error("expected None or Error");
                return {};
            } else
                return py::cast<T>(value);
        }();
        auto call = std::move(*shared);
        *shared = {};
        call(std::move(result));
    });
}

/** @brief 调用 Python 异步后端并隔离重复完成、异常及迟到结果。
 * @tparam T 原生成功结果类型。
 * @tparam Args 拥有型业务参数类型。
 * @param[in] callback Python 后端，接收业务参数和一次性 done，返回可选取消函数。
 * @param[in] handler 原生完成函数，异常时也至多通知一次。
 * @param[in] args 按值移交给 Python 的参数，可在调用后保留。
 * @return 可选非阻塞取消函数，按调用线程驱动约定使用。
 * @note Python 分类异常恢复为原生错误；后端抛异常后的晚到完成被忽略。
 */
template <class T, class... Args>
std::function<void()> invoke_backend(py::handle callback, std::function<void(Result<T>)> handler,
                                     Args... args) {
    py::gil_scoped_acquire acquire;
    auto pending = std::make_shared<std::function<void(Result<T>)>>(std::move(handler));
    auto done = completion_function<T>([pending](Result<T> result) {
        auto call = std::move(*pending);
        *pending = {};
        if (call) call(std::move(result));
    });
    auto fail = [&](Error error) {
        auto call = std::move(*pending);
        *pending = {};
        if (call) call(std::move(error));
    };
    try {
        auto cancel = py::reinterpret_borrow<py::object>(callback)(std::move(args)..., done);
        if (cancel.is_none()) return {};
        return py::cast<std::function<void()>>(cancel);
    } catch (py::error_already_set& error) {
        try {
            auto value = error.value();
            if (py::hasattr(value, "code") && py::hasattr(value, "offset") &&
                py::hasattr(value, "context") && py::hasattr(value, "remote_code")) {
                fail(Error{py::cast<ErrorCode>(value.attr("code")),
                           py::cast<std::size_t>(value.attr("offset")),
                           py::cast<std::string>(value.attr("context")),
                           py::cast<std::optional<std::uint8_t>>(value.attr("remote_code"))});
                return {};
            }
        } catch (const std::exception&) {
        }
        fail(Error{ErrorCode::invalid_value, 0, error.what()});
    } catch (const std::exception& error) {
        fail(Error{ErrorCode::invalid_value, 0, error.what()});
    }
    return {};
}
}  // namespace dlt698::python

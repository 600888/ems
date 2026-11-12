/** @file options_callbacks.hpp
 * @brief 为生成配置补充 RS-485 原生结果与 Python 回调的转换。
 */
#pragma once
#include <dlt698/transport/serial_link.hpp>

#include "callbacks.hpp"

namespace dlt698::python {
/** @brief 注册方向控制与排空回调字段，支持关键字构造及 None 清除。
 * @param[in,out] value 串行链路配置注册器。
 * @note 所有 Python hooks 只允许在调用线程驱动接口执行。
 */
inline void serial_callbacks(Struct<transport::SerialLinkOptions>& value) {
    value.property(
        "set_transmit",
        [](const transport::SerialLinkOptions& options)
            -> std::function<std::optional<Error>(bool)> {
            if (!options.set_transmit) return {};
            return [call = options.set_transmit](bool transmit) -> std::optional<Error> {
                auto result = call(transmit);
                return result ? std::nullopt : std::optional<Error>(result.error());
            };
        },
        [](transport::SerialLinkOptions& options, py::handle value) {
            auto call = py::cast<std::function<std::optional<Error>(bool)>>(value);
            options.set_transmit =
                call ? std::function<Result<void>(bool)>(
                           [call = std::move(call)](bool transmit) -> Result<void> {
                               auto error = call(transmit);
                               return error ? Result<void>(*error) : Result<void>{};
                           })
                     : std::function<Result<void>(bool)>{};
        });
    value.property(
        "async_drain",
        [](const transport::SerialLinkOptions& options)
            -> std::function<void(std::function<void(std::optional<Error>)>)> {
            if (!options.async_drain) return {};
            return [call = options.async_drain](std::function<void(std::optional<Error>)> done) {
                call([done = std::move(done)](Result<void> result) {
                    done(result ? std::nullopt : std::optional<Error>(result.error()));
                });
            };
        },
        [](transport::SerialLinkOptions& options, py::handle value) {
            auto call = py::cast<std::function<void(py::object)>>(value);
            options.async_drain =
                call ? std::function<void(transport::IChannel::WriteHandler)>(
                           [call = std::move(call)](transport::IChannel::WriteHandler done) {
                               py::gil_scoped_acquire acquire;
                               call(completion_function<void>(std::move(done)));
                           })
                     : std::function<void(transport::IChannel::WriteHandler)>{};
        });
}
}  // namespace dlt698::python

#pragma once
#include "default_logger.hpp"

/** @brief 输出函数开始或结束位置的调试日志。 */
#define LOG_FUNC_BEG LOG_DEBUG("begin func {}", __FUNCTION__);
#define LOG_FUNC_END LOG_DEBUG("end func {}", __FUNCTION__);

/** @brief 日志等级枚举别名。 */
using LV = spdlog::level::level_enum;

/** @brief 构造时和析构时分别记录函数边界。 */
template <LV lv = LV::debug>
class LogGuard {
   public:
    /**
     * @brief 记录操作开始，并保存结束日志所需的消息。
     * @param[in] msg 消息内容。
     */
    LogGuard(const std::string& msg) : _msg(msg) {
        switch (_lv) {
            case LV::debug:
                DefaultLogger::debug(fmt::format("{} begin", _msg).c_str());
                break;
            case LV::info:
            default:
                DefaultLogger::info(fmt::format("{} begin", _msg).c_str());
                break;
        }
    }

    /** @brief 记录操作结束。 */
    ~LogGuard() {
        switch (_lv) {
            case LV::debug:
                DefaultLogger::debug(fmt::format("{} end", _msg).c_str());
                break;
            case LV::info:
            default:
                DefaultLogger::info(fmt::format("{} end", _msg).c_str());
                break;
        }
    }

   private:
    LV _lv = lv;
    std::string _msg;
};

#define LOG_FUNC                                              \
    LogGuard<> logGuard(std::string("[")                      \
                            .append(__FILENAME__)             \
                            .append(":")                      \
                            .append(std::to_string(__LINE__)) \
                            .append("]<")                     \
                            .append(__FUNCTION__)             \
                            .append("> - ")                   \
                            .c_str());

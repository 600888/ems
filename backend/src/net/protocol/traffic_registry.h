#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>

#include "net/protocol/traffic_observer.h"

namespace ems::net::protocol {

/** @brief 为缺少观察器接口的集成传输对象绑定实例观察器，销毁前先停止传输线程。 */
class TrafficBinding {
   public:
    /**
     * @brief 登记传输对象；同一插件内的构建副本按对象地址查找。
     * @param[in] source 传输对象地址。
     * @param[in] observer 与协议实例共享的观察器。
     */
    TrafficBinding(const void* source, std::shared_ptr<TrafficObserver> observer)
        : source_(source) {
        auto& table = registry();
        std::lock_guard<std::mutex> lock(table.mutex);
        table.observers[source_] = std::move(observer);
    }

    /** @brief 移除已停止传输的观察登记。 */
    ~TrafficBinding() {
        auto& table = registry();
        std::lock_guard<std::mutex> lock(table.mutex);
        table.observers.erase(source_);
    }

    /** @brief 禁止复制登记。 @param[in] other 源登记。 */
    TrafficBinding(const TrafficBinding& other) = delete;
    /** @brief 禁止复制赋值。 @param[in] other 源登记。 @return 不可调用。 */
    TrafficBinding& operator=(const TrafficBinding& other) = delete;

    /**
     * @brief 观察构建副本实际收到或完成写入的字节。
     * @param[in] source 传输对象地址。
     * @param[in] direction 收发方向。
     * @param[in] bytes 借用字节。
     * @param[in] size 字节数。
     * @param[in] connection_id 连接标识；0 表示未区分。
     */
    static void emit(const void* source, int direction, const uint8_t* bytes, size_t size,
                     uint64_t connection_id = 0) noexcept {
        try {
            std::shared_ptr<TrafficObserver> observer;
            auto& table = registry();
            {
                std::lock_guard<std::mutex> lock(table.mutex);
                const auto found = table.observers.find(source);
                if (found == table.observers.end()) return;
                observer = found->second;
            }
            observer->emit(direction, bytes, size, connection_id);
        } catch (...) {
        }
    }

   private:
    struct Registry {
        std::mutex mutex;
        std::unordered_map<const void*, std::shared_ptr<TrafficObserver>> observers;
    };

    /** @brief 获取本插件共享的登记表。 @return 存活至模块卸载的登记表。 */
    static Registry& registry() {
        static Registry table;
        return table;
    }

    const void* source_;
};
}  // namespace ems::net::protocol

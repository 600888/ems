/**
 * @file backend_factory.h
 * @brief 实时后端工厂（access-design.md 第 5 节）
 *
 * 后端以稳定注册名注册（不使用不断扩大的业务枚举）。工厂显式注册
 * 后冻结；没有编译的后端被配置选中时 create 返回 Unsupported，不能
 * 悄悄替换成其他后端。注册失败不得覆盖既有类型。
 */
#pragma once

#include <common/core/call_context.h>
#include <common/core/result.h>
#include <rtdb/store/realtime_store.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace ems::rtdb {

/** @brief 后端规格；type 为稳定注册名，options 为后端声明的配置键值。 */
struct BackendSpec {
    std::string type;
    std::map<std::string, std::string> options;
};

using Creator =
    std::function<Result<std::unique_ptr<IRealtimeStore>>(const BackendSpec&, const CallContext&)>;

/** @brief 注册并按配置创建实时存储后端。 */
class BackendFactory {
   public:
    /**
     * @brief 注册后端创建器。
     * @param[in] type 稳定的后端类型名。
     * @param[in] creator 该类型的创建器。
     * @return 重复注册或冻结后返回 Conflict/NotApplied。
     */
    Result<bool> register_backend(std::string type, Creator creator) {
        if (frozen_) {
            return core::make_error(ErrorCode::Conflict, core::WriteEffect::NotApplied,
                                    "backend registry already frozen");
        }
        if (type.empty() || !creator) {
            return core::make_error(ErrorCode::InvalidArgument, core::WriteEffect::NotApplied,
                                    "backend type/creator is empty");
        }
        if (!creators_.emplace(std::move(type), std::move(creator)).second) {
            return core::make_error(ErrorCode::Conflict, core::WriteEffect::NotApplied,
                                    "backend already registered: " + type);
        }
        return true;
    }

    /** @brief 冻结注册表，拒绝后续注册。 */
    void freeze() noexcept { frozen_ = true; }

    /**
     * @brief 查询注册表是否已冻结。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool is_frozen() const noexcept { return frozen_; }

    /**
     * @brief 按规格创建后端。
     * @param[in] spec 后端类型及选项。
     * @param[in] ctx 调用预算。
     * @return 未注册类型返回 Unsupported，不自动降级。
     */
    Result<std::unique_ptr<IRealtimeStore>> create(const BackendSpec& spec,
                                                   const CallContext& ctx) const {
        auto it = creators_.find(spec.type);
        if (it == creators_.end()) {
            return core::make_error(ErrorCode::Unsupported, core::WriteEffect::NotApplicable,
                                    "unknown realtime backend: " + spec.type);
        }
        return it->second(spec, ctx);
    }

   private:
    std::map<std::string, Creator> creators_;
    bool frozen_{false};
};

}  // namespace ems::rtdb

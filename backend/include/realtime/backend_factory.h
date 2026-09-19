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
#include <realtime/realtime_store.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace ems::realtime {

/// 后端规格：type 为稳定注册名，options 为该后端声明的配置键值
struct BackendSpec {
    std::string type;
    std::map<std::string, std::string> options;
};

using Creator =
    std::function<Result<std::unique_ptr<IRealtimeStore>>(const BackendSpec&, const CallContext&)>;

class BackendFactory {
   public:
    /// 注册后端；重复类型或注册阶段结束后冻结时返回 Conflict/NotApplied
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

    /// 冻结注册表；冻结后 register_backend 一律失败
    void freeze() noexcept { frozen_ = true; }

    bool is_frozen() const noexcept { return frozen_; }

    /// 按规格创建后端；未注册类型返回 Unsupported（启动失败，不降级）
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

}  // namespace ems::realtime

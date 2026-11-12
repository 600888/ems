#include <algorithm>
#include <dlt698/common/md5.hpp>
#include <dlt698/service/advanced.hpp>
#include <map>
#include <mutex>

namespace dlt698::service {
namespace apdu = protocol::apdu;
// 导出基类构造/析构，支持安装包调用方在其他 DLL 中实现目标 provider。
IProxyProvider::IProxyProvider() = default;
IProxyProvider::~IProxyProvider() = default;

struct ProxyRouter::Impl {
    std::mutex mutex;
    std::map<Bytes, std::shared_ptr<session::Session>> targets;
};

ProxyRouter::ProxyRouter() : impl_(std::make_unique<Impl>()) {}

ProxyRouter::~ProxyRouter() = default;

Result<void> ProxyRouter::bind(model::Tsa server, std::shared_ptr<session::Session> target) {
    if (!target || server.value.size() < 2 || server.value.size() > 17 ||
        (server.value[0] & 0xc0) || (server.value[0] & 15) + 2u != server.value.size())
        return Error{ErrorCode::invalid_value, 0, "proxy target TSA/session"};
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->targets[std::move(server.value)] = std::move(target);
    return {};
}

IProxyProvider::Cancel ProxyRouter::async_request(model::Tsa server, apdu::Apdu request,
                                                  Handler handler) {
    std::shared_ptr<session::Session> target;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto it = impl_->targets.find(server.value);
        if (it != impl_->targets.end()) target = it->second;
    }
    if (!target) {
        handler(Error{ErrorCode::address_mismatch, 0, "proxy target not bound"});
        return {};
    }
    auto deliver = [handler](auto result) mutable {
        if (!result)
            handler(result.error());
        else
            handler(apdu::Apdu{std::move(result).value()});
    };
    if (auto v = std::get_if<apdu::GetRequest>(&request))
        target->async_get(std::move(v->attributes), v->list, deliver);
    else if (auto record = std::get_if<apdu::GetRecordRequest>(&request))
        target->async_get_record(std::move(record->records), record->list, deliver);
    else if (auto set = std::get_if<apdu::SetRequest>(&request))
        target->async_set(std::move(set->attributes), set->list, deliver);
    else if (auto action = std::get_if<apdu::ActionRequest>(&request))
        target->async_action(std::move(action->methods), action->list, deliver);
    else
        target->async_exchange(std::move(request), std::move(handler));
    return [weak = std::weak_ptr<session::Session>(target)] {
        if (const auto live = weak.lock()) live->cancel();
    };
}

namespace {
apdu::Apdu failed(const apdu::Apdu& request, std::uint8_t dar) {
    if (const auto q = std::get_if<apdu::GetRequest>(&request)) {
        apdu::GetResponse r;
        r.list = q->list;
        for (auto a : q->attributes) r.attributes.push_back({a, dar});
        return r;
    }
    if (const auto q = std::get_if<apdu::GetRecordRequest>(&request)) {
        apdu::GetRecordResponse r;
        r.list = q->list;
        for (const auto& a : q->records) r.records.push_back({a.attribute, a.columns, dar});
        return r;
    }
    if (const auto q = std::get_if<apdu::SetRequest>(&request)) {
        apdu::SetResponse r;
        r.list = q->list;
        for (const auto& a : q->attributes) r.attributes.push_back({a.attribute, dar});
        return r;
    }
    if (const auto q = std::get_if<apdu::ActionRequest>(&request)) {
        apdu::ActionResponse r;
        r.list = q->list;
        for (const auto& a : q->methods) r.methods.push_back({a.method, dar, {}});
        return r;
    }
    if (const auto q = std::get_if<apdu::SetThenGetRequest>(&request)) {
        apdu::SetThenGetResponse r;
        for (const auto& a : q->items) r.items.push_back({{a.set.attribute, dar}, {a.read, dar}});
        return r;
    }
    apdu::ActionThenGetResponse r;
    for (const auto& a : std::get<apdu::ActionThenGetRequest>(request).items)
        r.items.push_back({{a.action.method, dar, {}}, {a.read, dar}});
    return r;
}

template <class T>
apdu::Apdu target_request(const std::vector<T>& items) {
    if constexpr (std::is_same_v<T, model::Oad>)
        return apdu::GetRequest{0, true, items, {}};
    else if constexpr (std::is_same_v<T, apdu::SetAttribute>)
        return apdu::SetRequest{0, true, items, {}};
    else if constexpr (std::is_same_v<T, apdu::ActionMethod>)
        return apdu::ActionRequest{0, true, items, {}};
    else if constexpr (std::is_same_v<T, apdu::SetThenGet>)
        return apdu::SetThenGetRequest{0, items, {}};
    else
        return apdu::ActionThenGetRequest{0, items, {}};
}

bool target_matches(const apdu::Apdu& q, const apdu::Apdu& r) {
    if (apdu::advanced_capability(q) < 65) return apdu::advanced_matches(q, r);
    auto expected = failed(q, 255);
    if (expected.index() != r.index()) return false;
    // 比较完整描述符；结果 DAR/Data 内容由目标保留，不能用编码长度或 PIID 替代匹配。
    if (const auto a = std::get_if<apdu::GetResponse>(&expected)) {
        const auto& b = std::get<apdu::GetResponse>(r);
        if (a->list != b.list || a->attributes.size() != b.attributes.size()) return false;
        for (std::size_t i = 0; i < a->attributes.size(); ++i)
            if (!(a->attributes[i].attribute == b.attributes[i].attribute)) return false;
    } else if (const auto records = std::get_if<apdu::GetRecordResponse>(&expected)) {
        const auto& b = std::get<apdu::GetRecordResponse>(r);
        if (records->list != b.list || records->records.size() != b.records.size()) return false;
        for (std::size_t i = 0; i < records->records.size(); ++i)
            if (!(records->records[i].attribute == b.records[i].attribute) ||
                (!records->records[i].columns.empty() &&
                 !(records->records[i].columns == b.records[i].columns)))
                return false;
    } else if (const auto sets = std::get_if<apdu::SetResponse>(&expected)) {
        const auto& b = std::get<apdu::SetResponse>(r);
        if (sets->list != b.list || sets->attributes.size() != b.attributes.size()) return false;
        for (std::size_t i = 0; i < sets->attributes.size(); ++i)
            if (!(sets->attributes[i].attribute == b.attributes[i].attribute)) return false;
    } else {
        const auto& actions = std::get<apdu::ActionResponse>(expected);
        const auto& b = std::get<apdu::ActionResponse>(r);
        if (actions.list != b.list || actions.methods.size() != b.methods.size()) return false;
        for (std::size_t i = 0; i < actions.methods.size(); ++i)
            if (!(actions.methods[i].method == b.methods[i].method)) return false;
    }
    return true;
}

struct Job : std::enable_shared_from_this<Job> {
    std::weak_ptr<session::Session> session;
    std::shared_ptr<ObjectRegistry> objects;
    std::shared_ptr<IExecutor> executor;
    AdvancedServiceOptions options;
    apdu::Apdu request, response;
    session::Session::ExchangeHandler complete;
    std::shared_ptr<ITimer> timer, deadline;
    IProxyProvider::Cancel cancel;
    bool done = false;
    std::size_t index = 0;
    std::uint64_t generation = 0;

    struct Target {
        model::Tsa server;
        std::uint16_t timeout;
        apdu::Apdu request;
    };

    std::vector<Target> targets;

    bool active() const {
        const auto live = session.lock();
        return !done && live && live->state() == session::State::associated;
    }

    void finish(Result<apdu::Apdu> result) {
        if (done) return;
        done = true;
        ++generation;
        if (timer) timer->cancel();
        if (deadline) deadline->cancel();
        timer.reset();
        deadline.reset();
        cancel = {};
        auto handler = std::move(complete);
        if (handler) handler(std::move(result));
    }

    void stop_target() {
        auto stop = std::move(cancel);
        cancel = {};
        try {
            if (stop) stop();
        } catch (...) {
        }
    }

    void wait_read(std::uint8_t delay) {
        const auto self = shared_from_this();
        // 使用单调定时器，不阻塞执行器；延时零显式采用服务器配置。
        const auto seconds = delay ? delay : options.default_read_delay_seconds;
        timer = executor->schedule(std::chrono::seconds(seconds), [self] {
            self->timer.reset();
            if (!self->active()) {
                self->finish(Error{ErrorCode::closed, 0, "ThenGet association ended"});
                return;
            }
            if (const auto q = std::get_if<apdu::SetThenGetRequest>(&self->request)) {
                const auto a = q->items[self->index].read;
                std::get<apdu::SetThenGetResponse>(self->response).items[self->index].read = {
                    a, self->objects->read(a)};
            } else {
                const auto a =
                    std::get<apdu::ActionThenGetRequest>(self->request).items[self->index].read;
                std::get<apdu::ActionThenGetResponse>(self->response).items[self->index].read = {
                    a, self->objects->read(a)};
            }
            ++self->index;
            self->then_step();
        });
    }

    void then_step() {
        if (!active()) {
            finish(Error{ErrorCode::closed, 0, "ThenGet association ended"});
            return;
        }
        if (const auto q = std::get_if<apdu::SetThenGetRequest>(&request)) {
            if (index == q->items.size()) {
                finish(std::move(response));
                return;
            }
            const auto& a = q->items[index];
            std::get<apdu::SetThenGetResponse>(response).items[index].set.dar =
                objects->write(a.set.attribute, a.set.value);
            wait_read(a.delay_seconds);
        } else {
            const auto& q2 = std::get<apdu::ActionThenGetRequest>(request);
            if (index == q2.items.size()) {
                finish(std::move(response));
                return;
            }
            const auto& a = q2.items[index];
            auto result = objects->invoke(a.action.method, a.action.parameter);
            std::get<apdu::ActionThenGetResponse>(response).items[index].action = {
                a.action.method, result.dar, std::move(result.data)};
            wait_read(a.delay_seconds);
        }
    }

    void store_target(apdu::Apdu value) {
        auto& payload = std::get<apdu::ProxyResponse>(response).payload;
        switch (payload.index()) {
            case 0:
                std::get<0>(payload)[index].items =
                    std::move(std::get<apdu::GetResponse>(value).attributes);
                break;
            case 1:
                std::get<1>(payload).record =
                    std::move(std::get<apdu::GetRecordResponse>(value).records.front());
                break;
            case 2:
                std::get<2>(payload)[index].items =
                    std::move(std::get<apdu::SetResponse>(value).attributes);
                break;
            case 3:
                std::get<3>(payload)[index].items =
                    std::move(std::get<apdu::SetThenGetResponse>(value).items);
                break;
            case 4:
                std::get<4>(payload)[index].items =
                    std::move(std::get<apdu::ActionResponse>(value).methods);
                break;
            case 5:
                std::get<5>(payload)[index].items =
                    std::move(std::get<apdu::ActionThenGetResponse>(value).items);
                break;
            default:
                break;
        }
    }

    void target_complete(std::uint64_t token, Result<apdu::Apdu> value) {
        if (done || token != generation) return;
        ++generation;
        if (timer) timer->cancel();
        timer.reset();
        cancel = {};
        if (!value || !target_matches(targets[index].request, value.value())) {
            const auto dar = !value && value.error().code == ErrorCode::timeout            ? 2
                             : !value && value.error().code == ErrorCode::address_mismatch ? 4
                                                                                           : 255;
            store_target(failed(targets[index].request, static_cast<std::uint8_t>(dar)));
        } else
            store_target(std::move(value).value());
        ++index;
        proxy_step();
    }

    void proxy_step() {
        if (!active()) {
            stop_target();
            finish(Error{ErrorCode::closed, 0, "proxy association ended"});
            return;
        }
        if (index == targets.size()) {
            finish(std::move(response));
            return;
        }
        const auto self = shared_from_this();
        const auto token = ++generation;
        const auto seconds =
            targets[index].timeout ? targets[index].timeout : options.default_proxy_timeout_seconds;
        timer = executor->schedule(std::chrono::seconds(seconds), [self, token] {
            if (self->done || token != self->generation) return;
            self->stop_target();
            self->target_complete(token, Error{ErrorCode::timeout, 0, "proxy target timeout"});
        });
        if (!options.proxy) {
            target_complete(token, Error{ErrorCode::address_mismatch, 0, "proxy provider absent"});
            return;
        }
        try {
            cancel = options.proxy->async_request(
                targets[index].server, targets[index].request,
                [weak = std::weak_ptr<Job>(self), executor = executor,
                 token](Result<apdu::Apdu> value) mutable {
                    executor->post([weak, token, value = std::move(value)]() mutable {
                        if (const auto live = weak.lock())
                            live->target_complete(token, std::move(value));
                    });
                });
        } catch (...) {
            target_complete(token, Error{ErrorCode::invalid_value, 0, "proxy provider exception"});
        }
    }

    template <std::size_t I>
    void prepare_targets(const apdu::ProxyRequest& q) {
        auto& payload = std::get<apdu::ProxyResponse>(response).payload;
        if constexpr (I == 1) {
            const auto& a = std::get<I>(q.payload);
            auto inner = apdu::Apdu{apdu::GetRecordRequest{0, false, {a.record}, {}}};
            payload = apdu::ProxyRecordResponse{
                a.server, std::get<apdu::GetRecordResponse>(failed(inner, 2)).records.front()};
            targets.push_back({a.server, q.timeout_seconds, std::move(inner)});
        } else {
            using Results = std::variant_alternative_t<I, apdu::ProxyResponsePayload>;
            payload = Results{};
            for (const auto& a : std::get<I>(q.payload)) {
                auto inner = target_request(a.items);
                auto failure = failed(inner, 2);
                typename Results::value_type result;
                result.server = a.server;
                if constexpr (I == 0 || I == 2)
                    result.items = std::move(
                        std::get<std::conditional_t<I == 0, apdu::GetResponse, apdu::SetResponse>>(
                            failure)
                            .attributes);
                else if constexpr (I == 4)
                    result.items = std::move(std::get<apdu::ActionResponse>(failure).methods);
                else if constexpr (I == 3)
                    result.items = std::move(std::get<apdu::SetThenGetResponse>(failure).items);
                else
                    result.items = std::move(std::get<apdu::ActionThenGetResponse>(failure).items);
                std::get<I>(payload).push_back(std::move(result));
                targets.push_back({a.server, a.timeout_seconds, std::move(inner)});
            }
        }
    }

    void trans_start(const apdu::ProxyTransRequest& q) {
        apdu::ProxyResponse r;
        r.payload = apdu::ProxyTransResponse{q.port, std::uint8_t{2}};
        response = r;
        const auto self = shared_from_this();
        const auto seconds = q.response_timeout_seconds ? q.response_timeout_seconds
                                                        : options.default_proxy_timeout_seconds;
        deadline = executor->schedule(std::chrono::seconds(seconds), [self] {
            self->stop_target();
            self->finish(std::move(self->response));
        });
        if (!options.trans) {
            std::get<apdu::ProxyTransResponse>(std::get<apdu::ProxyResponse>(response).payload)
                .result = std::uint8_t{4};
            finish(std::move(response));
            return;
        }
        cancel = options.trans(q, [weak = std::weak_ptr<Job>(self), executor = executor](
                                      Result<apdu::ProxyTransResponse> value) mutable {
            executor->post([weak, value = std::move(value)]() mutable {
                const auto self = weak.lock();
                if (!self) return;
                if (!self->active()) return;
                if (!value || !(value.value().port ==
                                std::get<apdu::ProxyTransResponse>(
                                    std::get<apdu::ProxyResponse>(self->response).payload)
                                    .port))
                    std::get<apdu::ProxyTransResponse>(
                        std::get<apdu::ProxyResponse>(self->response).payload)
                        .result = std::uint8_t{255};
                else
                    std::get<apdu::ProxyResponse>(self->response).payload =
                        std::move(value).value();
                self->finish(std::move(self->response));
            });
        });
    }

    void start() {
        if (const auto q = std::get_if<apdu::GetMd5Request>(&request)) {
            apdu::GetMd5Response r;
            r.attribute = q->attribute;
            const auto v = objects->read(q->attribute);
            if (const auto dar = std::get_if<std::uint8_t>(&v))
                r.result = *dar;
            else {
                const auto encoded = codec::encode_data(std::get<model::Data>(v), options.limits);
                if (!encoded)
                    r.result = std::uint8_t{255};
                else
                    r.result = md5(encoded.value());
            }
            finish(apdu::Apdu{r});
            return;
        }
        if (std::holds_alternative<apdu::SetThenGetRequest>(request) ||
            std::holds_alternative<apdu::ActionThenGetRequest>(request)) {
            response = failed(request, 255);
            then_step();
            return;
        }
        const auto& q = std::get<apdu::ProxyRequest>(request);
        if (q.payload.index() == 6) {
            trans_start(std::get<6>(q.payload));
            return;
        }
        response = apdu::ProxyResponse{};
        switch (q.payload.index()) {
            case 0:
                prepare_targets<0>(q);
                break;
            case 1:
                prepare_targets<1>(q);
                break;
            case 2:
                prepare_targets<2>(q);
                break;
            case 3:
                prepare_targets<3>(q);
                break;
            case 4:
                prepare_targets<4>(q);
                break;
            default:
                prepare_targets<5>(q);
                break;
        }
        const auto self = shared_from_this();
        // 总超时到达时保留已完成目标；未完成项预填 DAR=2，迟到回调按 generation 丢弃。
        deadline = executor->schedule(std::chrono::seconds(q.timeout_seconds), [self] {
            self->stop_target();
            self->finish(std::move(self->response));
        });
        proxy_step();
    }
};
}  // namespace

AdvancedService::AdvancedService(std::shared_ptr<session::Session> session,
                                 std::shared_ptr<ObjectRegistry> objects,
                                 std::shared_ptr<IExecutor> executor,
                                 AdvancedServiceOptions options) {
    if (!session || !objects || !executor || !options.default_proxy_timeout_seconds ||
        !options.limits.max_elements || !options.limits.max_data_bytes)
        throw std::invalid_argument("advanced service options");
    session->set_advanced_handler(
        [weak = std::weak_ptr<session::Session>(session), objects = std::move(objects),
         executor = std::move(executor), options = std::move(options)](
            apdu::Apdu request, session::Session::ExchangeHandler complete) {
            auto job = std::make_shared<Job>();
            job->session = weak;
            job->objects = objects;
            job->executor = executor;
            job->options = options;
            job->request = std::move(request);
            job->complete = std::move(complete);
            try {
                job->start();
            } catch (...) {
                job->stop_target();
                job->finish(Error{ErrorCode::invalid_value, 0, "advanced service exception"});
            }
            return [weak_job = std::weak_ptr<Job>(job), executor] {
                executor->post([weak_job] {
                    if (const auto live = weak_job.lock()) {
                        live->stop_target();
                        live->finish(Error{ErrorCode::cancelled, 0, "advanced backend cancelled"});
                    }
                });
            };
        });
}
}  // namespace dlt698::service

#include <dlt698/service/point_probe.hpp>
#include <stdexcept>

namespace dlt698::service {
namespace {
struct ProbeState : std::enable_shared_from_this<ProbeState> {
    std::shared_ptr<session::Session> session;
    ProbeOptions options;
    std::vector<standard::ReadBatch> batches;
    std::vector<PointResult> results;
    std::size_t next = 0;
    std::function<void(Result<std::vector<PointResult>>)> handler;

    void run() {
        if (next == batches.size()) {
            auto finished = std::move(handler);
            finished(std::move(results));
            return;
        }
        const auto batch_index = next++;
        const auto& batch = batches[batch_index];
        // 回调持有操作状态及会话；每批完成后才提交下一批，避免争用单事务槽和扩大在途内存。
        session->async_get(
            batch.attributes, batch.list,
            [self = shared_from_this(), batch_index](Result<protocol::apdu::GetResponse> response) {
                const auto& request = self->batches[batch_index];
                if (!response) {
                    for (const auto& attribute : request.attributes)
                        self->results.push_back({attribute, response.error(), false, {}});
                } else {
                    const auto& attributes = response.value().attributes;
                    for (std::size_t i = 0; i < request.attributes.size(); ++i) {
                        const auto attribute = request.attributes[i];
                        if (attributes.size() != request.attributes.size() ||
                            !(attributes[i].attribute == attribute)) {
                            self->results.push_back(
                                {attribute,
                                 Error{ErrorCode::invalid_value, 0, "probe response descriptors"},
                                 false,
                                 {}});
                            continue;
                        }
                        const auto& item = attributes[i];
                        if (const auto dar = std::get_if<std::uint8_t>(&item.result)) {
                            self->results.push_back({attribute, *dar, false, {}});
                        } else {
                            const auto& value = std::get<model::Data>(item.result);
                            PointResult result{attribute, value, false, {}};
                            if (standard::find_attribute(attribute)) {
                                result.schema_checked = true;
                                auto valid = standard::validate_value(
                                    attribute, value, self->options.layout, self->options.limits);
                                if (!valid) result.validation_error = valid.error();
                            }
                            self->results.push_back(std::move(result));
                        }
                    }
                }
                self->run();
            });
    }
};
}  // namespace

void async_probe_points(std::shared_ptr<session::Session> session,
                        standard::Capabilities capabilities, std::vector<model::Oad> attributes,
                        ProbeOptions options,
                        std::function<void(Result<std::vector<PointResult>>)> handler) {
    if (!session || !handler) throw std::invalid_argument("null probe session/handler");
    auto valid = standard::validate_layout(options.layout);
    if (!valid) {
        handler(valid.error());
        return;
    }
    auto plan = standard::plan_reads(capabilities, attributes, options.batch_size, options.limits);
    if (!plan) {
        handler(plan.error());
        return;
    }
    auto state = std::make_shared<ProbeState>();
    state->session = std::move(session);
    state->options = std::move(options);
    state->batches = std::move(plan).value();
    state->results.reserve(attributes.size());
    state->handler = std::move(handler);
    state->run();
}
}  // namespace dlt698::service

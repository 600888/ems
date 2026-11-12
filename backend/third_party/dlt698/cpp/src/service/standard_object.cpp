#include <dlt698/service/standard_object.hpp>
#include <dlt698/standard/records.hpp>

namespace dlt698::service {
namespace {
class StandardProvider final : public IObjectProvider {
   public:
    StandardProvider(std::shared_ptr<IObjectProvider> provider, standard::DeviceLayout layout,
                     Limits limits)
        : provider_(std::move(provider)), layout_(layout), limits_(limits) {}

    ObjectValue read(const model::Oad& attribute) override {
        auto valid = standard::validate_oad(attribute, layout_);
        if (!valid) {
            if (valid.error().code == ErrorCode::unsupported_tag) return std::uint8_t{4};
            if (valid.error().code == ErrorCode::unsupported_service) return std::uint8_t{3};
            return std::uint8_t{8};
        }
        // 无目录/适配器锁，原始 OAD 及索引完整交给应用；不读取第二次以伪造跨查询快照。
        auto value = provider_->read(attribute);
        if (const auto data = std::get_if<model::Data>(&value))
            if (!standard::validate_value(attribute, *data, layout_, limits_))
                return std::uint8_t{7};
        return value;
    }

    protocol::apdu::RecordResult read_record(const protocol::apdu::GetRecord& query) override {
        auto fail = [&](std::uint8_t dar) {
            return protocol::apdu::RecordResult{query.attribute, query.columns, dar};
        };
        auto valid = standard::validate_record_query(query, layout_, limits_);
        if (!valid) {
            if (valid.error().code == ErrorCode::unsupported_tag) return fail(4);
            if (valid.error().code == ErrorCode::unsupported_service ||
                valid.error().code == ErrorCode::resource_limit)
                return fail(3);
            return fail(8);
        }
        // 无适配器锁，原样转发行列条件；每次返回值独立持有快照，异常仍交给目录隔离。
        auto result = provider_->read_record(query);
        if (!standard::validate_record_result(query, result, layout_, limits_)) return fail(7);
        return result;
    }

   private:
    std::shared_ptr<IObjectProvider> provider_;
    const standard::DeviceLayout layout_;
    const Limits limits_;
};
}  // namespace

Result<ObjectSchema> make_object_schema(std::uint16_t oi,
                                        const std::vector<std::uint8_t>& attributes) {
    const auto object = standard::find_object(oi);
    if (!object) return Error{ErrorCode::unsupported_tag, 0, "standard object"};
    if (attributes.empty() || attributes.size() > 31)
        return Error{ErrorCode::invalid_value, 0, "standard attribute selection"};
    ObjectSchema schema{oi, object->name, {}, {}};
    bool seen[32]{};
    for (const auto number : attributes) {
        if (!number || number > 31 || seen[number])
            return Error{ErrorCode::invalid_value, 0, "standard attribute number"};
        const auto definition = standard::find_attribute({oi, number, 0});
        if (!definition) return Error{ErrorCode::unsupported_tag, 0, "standard attribute"};
        seen[number] = true;
        // 标准可写属性也不自动授权；复位/执行等方法没有真实实现时不注册。
        schema.attributes.push_back(
            {number, definition->type, definition->readable, false, definition->record});
    }
    return schema;
}

Result<void> register_standard_object(ObjectRegistry& registry, std::uint16_t oi,
                                      const std::vector<std::uint8_t>& attributes,
                                      std::shared_ptr<IObjectProvider> provider,
                                      const standard::DeviceLayout& layout, const Limits& limits) {
    if (!provider) return Error{ErrorCode::invalid_value, 0, "standard provider"};
    auto valid = standard::validate_layout(layout);
    if (!valid) return valid;
    auto schema = make_object_schema(oi, attributes);
    if (!schema) return schema.error();
    auto adapter = std::make_shared<StandardProvider>(std::move(provider), layout, limits);
    return registry.register_object(std::move(schema).value(), std::move(adapter));
}
}  // namespace dlt698::service

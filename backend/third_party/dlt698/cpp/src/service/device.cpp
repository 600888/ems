#include <dlt698/codec/data_codec.hpp>
#include <dlt698/service/device.hpp>
#include <map>
#include <mutex>

namespace dlt698::service {
namespace {
/** @brief 不可变对象快照；目录在发布时整体替换提供者，读者无需持有设备锁。 */
class Snapshot final : public IObjectProvider {
   public:
    std::map<std::uint8_t, model::Data> values;
    std::map<std::uint8_t, std::size_t> sizes;
    ObjectSchema schema;

    ObjectValue read(const model::Oad& attribute) override {
        if (attribute.attribute & 0xe0) return std::uint8_t{3};
        const auto it = values.find(attribute.attribute);
        if (it == values.end()) return std::uint8_t{4};
        const auto& value = it->second;
        if (!attribute.index) return value;
        const std::vector<model::Data>* elements = nullptr;
        if (value.type() == model::DataType::array) elements = &value.as<model::Array>().value;
        if (value.type() == model::DataType::structure)
            elements = &value.as<model::Structure>().value;
        if (!elements || attribute.index > elements->size()) return std::uint8_t{8};
        return (*elements)[attribute.index - 1];
    }
};
}  // namespace

struct Device::Impl {
    DeviceOptions options;
    std::shared_ptr<ObjectRegistry> objects = std::make_shared<ObjectRegistry>();
    std::map<std::uint16_t, ObjectSchema> definitions;
    std::map<std::uint16_t, std::shared_ptr<Snapshot>> snapshots;
    std::size_t bytes = 0;
    std::size_t attributes = 0;
    std::mutex mutex;

    explicit Impl(DeviceOptions value) : options(std::move(value)) {}

    Result<void> publish(model::Oad attribute, model::Data value) {
        if (!attribute.attribute || attribute.attribute > 31 || attribute.index)
            return Error{ErrorCode::invalid_value, 0, "complete attribute OAD required"};
        AttributeSchema selected;
        const auto definition = definitions.find(attribute.oi);
        if (definition != definitions.end()) {
            bool found = false;
            for (const auto& a : definition->second.attributes)
                if (a.number == attribute.attribute) {
                    selected = a;
                    found = true;
                }
            if (!found) return Error{ErrorCode::unsupported_tag, 0, "undefined custom attribute"};
            if (selected.type != value.type())
                return Error{ErrorCode::invalid_value, 0, "custom attribute type"};
        } else {
            auto valid = standard::validate_value(attribute, value, options.layout, options.limits);
            if (!valid) return valid.error();
            const auto standard = standard::find_attribute(attribute);
            if (!standard || !standard->readable)
                return Error{ErrorCode::unsupported_service, 0, "unreadable standard attribute"};
            selected = {attribute.attribute, standard->type, true, false, false};
        }
        auto encoded = codec::encode_data(value, options.limits);
        if (!encoded) return encoded.error();
        const auto current = snapshots.find(attribute.oi);
        const bool new_object = current == snapshots.end();
        const bool new_attribute =
            new_object || !current->second->values.count(attribute.attribute);
        const auto old_bytes = new_attribute ? 0 : current->second->sizes.at(attribute.attribute);
        const auto size = encoded.value().size();
        if (size > options.max_value_bytes - (bytes - old_bytes))
            return Error{ErrorCode::resource_limit, 0, "device value bytes"};
        // 自定义声明已预占对象/属性预算；标准点位仅在首次成功发布时占用。
        if (definition == definitions.end()) {
            if (new_object) {
                std::size_t standard_objects = 0;
                for (const auto& entry : snapshots)
                    if (!definitions.count(entry.first)) ++standard_objects;
                if (standard_objects + definitions.size() >= options.max_objects)
                    return Error{ErrorCode::resource_limit, 0, "device objects"};
            }
            if (new_attribute && attributes >= options.max_attributes)
                return Error{ErrorCode::resource_limit, 0, "device attributes"};
        }
        auto next = new_object ? std::make_shared<Snapshot>()
                               : std::make_shared<Snapshot>(*current->second);
        if (new_object) {
            next->schema.oi = attribute.oi;
            next->schema.name = definition == definitions.end()
                                    ? standard::find_object(attribute.oi)->name
                                    : definition->second.name;
        }
        next->values.insert_or_assign(attribute.attribute, std::move(value));
        next->sizes.insert_or_assign(attribute.attribute, size);
        if (new_attribute) next->schema.attributes.push_back(selected);
        // 先准备完整状态；目录原子切换 schema/provider 后只做不抛异常的交换与计数。
        // 运行中新增同一 OI 的其他属性时，旧属性及已交付的快照保持有效。
        auto prepared = snapshots;
        prepared.insert_or_assign(attribute.oi, next);
        objects->publish_object(next->schema, next);
        snapshots.swap(prepared);
        bytes = bytes - old_bytes + size;
        if (new_attribute && definition == definitions.end()) ++attributes;
        return {};
    }
};

Device::Device(DeviceOptions options) {
    if (!standard::validate_layout(options.layout) || !options.max_objects ||
        !options.max_attributes || !options.max_value_bytes || !options.limits.max_data_bytes ||
        !options.limits.max_elements || !options.limits.max_depth)
        throw std::invalid_argument("invalid device options");
    impl_ = std::make_unique<Impl>(std::move(options));
}

Device::~Device() = default;

Result<void> Device::set(model::Oad attribute, model::Data value) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->publish(attribute, std::move(value));
}

Result<void> Device::set_element(model::Oad attribute, model::Data value) {
    if (!attribute.index || !attribute.attribute || attribute.attribute > 31)
        return Error{ErrorCode::invalid_value, 0, "element OAD required"};
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto object = impl_->snapshots.find(attribute.oi);
    if (object == impl_->snapshots.end())
        return Error{ErrorCode::unsupported_tag, 0, "unpublished object"};
    const auto it = object->second->values.find(attribute.attribute);
    if (it == object->second->values.end())
        return Error{ErrorCode::unsupported_tag, 0, "unpublished attribute"};
    const auto& complete = it->second;
    const std::vector<model::Data>* elements = nullptr;
    if (complete.type() == model::DataType::array) elements = &complete.as<model::Array>().value;
    if (complete.type() == model::DataType::structure)
        elements = &complete.as<model::Structure>().value;
    if (!elements || attribute.index > elements->size() ||
        (*elements)[attribute.index - 1].type() != value.type())
        return Error{ErrorCode::invalid_value, 0, "element index/type"};
    auto changed = *elements;
    changed[attribute.index - 1] = std::move(value);
    attribute.index = 0;
    return impl_->publish(attribute, complete.type() == model::DataType::array
                                         ? model::Data{model::Array{std::move(changed)}}
                                         : model::Data{model::Structure{std::move(changed)}});
}

ObjectValue Device::get(model::Oad attribute) const { return impl_->objects->read(attribute); }

Result<void> Device::define(ObjectSchema schema) {
    if (standard::find_object(schema.oi) || !schema.methods.empty() || schema.attributes.empty() ||
        schema.attributes.size() > 31)
        return Error{ErrorCode::invalid_value, 0, "custom read-only schema required"};
    bool seen[32]{};
    for (const auto& a : schema.attributes) {
        if (!a.number || a.number > 31 || seen[a.number] || !a.readable || a.writable || a.record)
            return Error{ErrorCode::invalid_value, 0, "custom attribute schema"};
        seen[a.number] = true;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->definitions.count(schema.oi) || impl_->snapshots.count(schema.oi))
        return Error{ErrorCode::busy, 0, "duplicate device OI"};
    // 已发布自定义对象也在 definitions 中，不能重复计算对象数。
    std::size_t standard_objects = 0;
    for (const auto& entry : impl_->snapshots)
        if (!impl_->definitions.count(entry.first)) ++standard_objects;
    if (standard_objects + impl_->definitions.size() >= impl_->options.max_objects ||
        schema.attributes.size() > impl_->options.max_attributes - impl_->attributes)
        return Error{ErrorCode::resource_limit, 0, "device schema budget"};
    const auto count = schema.attributes.size();
    const auto oi = schema.oi;
    impl_->definitions.emplace(oi, std::move(schema));
    impl_->attributes += count;
    return {};
}

std::shared_ptr<ObjectRegistry> Device::objects() const { return impl_->objects; }
}  // namespace dlt698::service

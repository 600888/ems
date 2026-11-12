#include <dlt698/service/object.hpp>
#include <map>
#include <mutex>

namespace dlt698::service {
std::uint8_t IObjectProvider::write(const model::Oad&, const model::Data&) { return 3; }

ActionValue IObjectProvider::invoke(const model::Omd&, const model::Data&) { return {3, {}}; }

protocol::apdu::RecordResult IObjectProvider::read_record(const protocol::apdu::GetRecord& q) {
    return {q.attribute, q.columns, std::uint8_t{3}};
}

struct ObjectRegistry::Impl {
    struct Entry {
        ObjectSchema schema;
        std::shared_ptr<IObjectProvider> provider;
    };

    std::map<std::uint16_t, Entry> objects;
    mutable std::mutex mutex;
};

ObjectRegistry::ObjectRegistry() : impl_(std::make_unique<Impl>()) {}

ObjectRegistry::~ObjectRegistry() = default;

void ObjectRegistry::publish_object(ObjectSchema schema,
                                    std::shared_ptr<IObjectProvider> provider) {
    const auto oi = schema.oi;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    // schema 与数据快照作为同一目录项切换，已开始的读取继续持有旧提供者。
    // 仅 Device 的受控发布路径可替换；公开 register_object 仍拒绝重复 OI。
    impl_->objects.insert_or_assign(oi, Impl::Entry{std::move(schema), std::move(provider)});
}

Result<void> ObjectRegistry::register_object(ObjectSchema schema,
                                             std::shared_ptr<IObjectProvider> provider) {
    if (!provider || (schema.attributes.empty() && schema.methods.empty()) ||
        schema.attributes.size() > 31 || schema.methods.size() > 255)
        return Error{ErrorCode::invalid_value, 0, "object schema/provider"};
    bool numbers[32]{};
    for (const auto& attr : schema.attributes) {
        if (!attr.number || attr.number > 31 || numbers[attr.number])
            return Error{ErrorCode::invalid_value, 0, "attribute schema"};
        numbers[attr.number] = true;
    }
    bool method_numbers[256]{};
    for (const auto& method : schema.methods) {
        if (!method.number || method_numbers[method.number])
            return Error{ErrorCode::invalid_value, 0, "method schema"};
        method_numbers[method.number] = true;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->objects.count(schema.oi)) return Error{ErrorCode::busy, 0, "duplicate OI"};
    const auto oi = schema.oi;
    impl_->objects.emplace(oi, Impl::Entry{std::move(schema), std::move(provider)});
    return {};
}

ObjectValue ObjectRegistry::read(const model::Oad& attribute) const {
    std::shared_ptr<IObjectProvider> provider;
    std::optional<AttributeSchema> schema;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto it = impl_->objects.find(attribute.oi);
        if (it == impl_->objects.end()) return std::uint8_t{4};
        for (const auto& a : it->second.schema.attributes)
            if (a.number == (attribute.attribute & 31)) {
                schema = a;
                break;
            }
        if (!schema) return std::uint8_t{4};
        provider = it->second.provider;
    }
    if (!schema->readable || schema->record) return std::uint8_t{3};
    // provider 属于应用代码，不能在目录锁内调用，避免重入注册或其他业务锁造成死锁。
    try {
        auto value = provider->read(attribute);
        if (!attribute.index && std::holds_alternative<model::Data>(value) &&
            std::get<model::Data>(value).type() != schema->type)
            return std::uint8_t{7};
        return value;
    } catch (...) {
        return std::uint8_t{255};
    }
}

protocol::apdu::RecordResult ObjectRegistry::read_record(const protocol::apdu::GetRecord& q) const {
    auto fail = [&](std::uint8_t dar) {
        return protocol::apdu::RecordResult{q.attribute, q.columns, dar};
    };
    std::shared_ptr<IObjectProvider> provider;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto it = impl_->objects.find(q.attribute.oi);
        if (it == impl_->objects.end()) return fail(4);
        const AttributeSchema* schema = nullptr;
        for (const auto& attr : it->second.schema.attributes)
            if (attr.number == (q.attribute.attribute & 31)) schema = &attr;
        if (!schema) return fail(4);
        if (!schema->readable) return fail(3);
        if (!schema->record) return fail(5);
        provider = it->second.provider;
    }
    try {
        auto result = provider->read_record(q);
        if (!(result.attribute == q.attribute) ||
            (!q.columns.empty() && !(q.columns == result.columns)))
            return fail(7);
        if (const auto rows = std::get_if<std::vector<protocol::apdu::RecordRow>>(&result.result)) {
            if (!rows->empty() && result.columns.empty()) return fail(7);
            for (const auto& row : *rows)
                if (row.size() != result.columns.size()) return fail(7);
        }
        return result;
    } catch (...) {
        return fail(255);
    }
}

struct MemoryObject::Impl {
    std::mutex mutex;
    std::map<std::uint8_t,
             std::function<protocol::apdu::RecordResult(const protocol::apdu::GetRecord&)>>
        records;
    std::map<std::uint8_t, model::Data> attributes;
    std::map<std::uint8_t, std::function<ActionValue(const model::Omd&, const model::Data&)>>
        methods;
};

std::uint8_t ObjectRegistry::write(const model::Oad& attribute, const model::Data& value) const {
    std::shared_ptr<IObjectProvider> provider;
    std::optional<AttributeSchema> schema;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto it = impl_->objects.find(attribute.oi);
        if (it == impl_->objects.end()) return 4;
        for (const auto& a : it->second.schema.attributes)
            if (a.number == (attribute.attribute & 31)) schema = a;
        if (!schema) return 4;
        provider = it->second.provider;
    }
    if (!schema->writable || schema->record) return 3;
    if (!attribute.index && value.type() != schema->type) return 7;
    try {
        return provider->write(attribute, value);
    } catch (...) {
        return 255;
    }
}

ActionValue ObjectRegistry::invoke(const model::Omd& method, const model::Data& parameter) const {
    std::shared_ptr<IObjectProvider> provider;
    std::optional<MethodSchema> schema;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto it = impl_->objects.find(method.oi);
        if (it == impl_->objects.end()) return {4, {}};
        for (const auto& m : it->second.schema.methods)
            if (m.number == method.method) schema = m;
        if (!schema) return {4, {}};
        provider = it->second.provider;
    }
    if (!schema->executable) return {3, {}};
    if (schema->parameter_type && parameter.type() != *schema->parameter_type) return {7, {}};
    try {
        auto result = provider->invoke(method, parameter);
        if (result.data && schema->return_type && result.data->type() != *schema->return_type)
            return {7, {}};
        return result;
    } catch (...) {
        return {255, {}};
    }
}

void MemoryObject::bind_record(
    std::uint8_t attribute,
    std::function<protocol::apdu::RecordResult(const protocol::apdu::GetRecord&)> handler) {
    if (!attribute || attribute > 31 || !handler) throw std::invalid_argument("record callback");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->records[attribute] = std::move(handler);
}

protocol::apdu::RecordResult MemoryObject::read_record(const protocol::apdu::GetRecord& q) {
    std::function<protocol::apdu::RecordResult(const protocol::apdu::GetRecord&)> handler;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto it = impl_->records.find(q.attribute.attribute & 31);
        if (it == impl_->records.end()) return {q.attribute, q.columns, std::uint8_t{4}};
        handler = it->second;
    }
    // 复制处理器后释放对象锁，允许记录后端在查询过程中读取相关属性。
    return handler(q);
}

MemoryObject::MemoryObject() : impl_(std::make_unique<Impl>()) {}

MemoryObject::~MemoryObject() = default;

void MemoryObject::set(std::uint8_t attribute, model::Data value) {
    if (!attribute || attribute > 31) throw std::invalid_argument("attribute number");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->attributes.insert_or_assign(attribute, std::move(value));
}

ObjectValue MemoryObject::read(const model::Oad& attribute) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto it = impl_->attributes.find(attribute.attribute);
    if (it == impl_->attributes.end()) return std::uint8_t{4};
    const auto& value = it->second;
    if (!attribute.index) return value;
    const std::vector<model::Data>* elements = nullptr;
    if (value.type() == model::DataType::array) elements = &value.as<model::Array>().value;
    if (value.type() == model::DataType::structure) elements = &value.as<model::Structure>().value;
    if (!elements || attribute.index > elements->size()) return std::uint8_t{8};
    return (*elements)[attribute.index - 1];
}

std::uint8_t MemoryObject::write(const model::Oad& attribute, const model::Data& value) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto it = impl_->attributes.find(attribute.attribute);
    if (it == impl_->attributes.end()) return 4;
    if (attribute.index) {
        const auto& target = it->second;
        const std::vector<model::Data>* elements = nullptr;
        if (target.type() == model::DataType::array) elements = &target.as<model::Array>().value;
        if (target.type() == model::DataType::structure)
            elements = &target.as<model::Structure>().value;
        if (!elements || attribute.index > elements->size()) return 8;
        if ((*elements)[attribute.index - 1].type() != value.type()) return 7;
        // Data 对外只读；复制容器再替换整体，已经交付给读取方的旧快照不会被修改。
        auto changed = *elements;
        changed[attribute.index - 1] = value;
        if (target.type() == model::DataType::array)
            it->second = model::Array{std::move(changed)};
        else
            it->second = model::Structure{std::move(changed)};
        return 0;
    }
    if (it->second.type() != value.type()) return 7;
    it->second = value;
    return 0;
}

void MemoryObject::bind_method(
    std::uint8_t number,
    std::function<ActionValue(const model::Omd&, const model::Data&)> handler) {
    if (!number || !handler) throw std::invalid_argument("method handler");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->methods.insert_or_assign(number, std::move(handler));
}

ActionValue MemoryObject::invoke(const model::Omd& method, const model::Data& parameter) {
    if (method.mode) return {3, {}};
    std::function<ActionValue(const model::Omd&, const model::Data&)> handler;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto it = impl_->methods.find(method.method);
        if (it == impl_->methods.end()) return {4, {}};
        handler = it->second;
    }
    // 方法可能重入对象或目录；复制回调后释放对象锁，再进入应用代码。
    return handler(method, parameter);
}
}  // namespace dlt698::service

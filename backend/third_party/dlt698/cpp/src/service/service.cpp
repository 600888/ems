#include <dlt698/service/service.hpp>

namespace dlt698::service {
ClientService::ClientService(std::shared_ptr<session::Session> session)
    : session_(std::move(session)) {
    if (!session_) throw std::invalid_argument("null session");
}

void ClientService::async_get(model::Oad attribute,
                              std::function<void(Result<ObjectValue>)> handler) {
    session_->async_get(
        {attribute}, false,
        [handler = std::move(handler)](Result<protocol::apdu::GetResponse> result) mutable {
            if (handler) {
                if (!result)
                    handler(result.error());
                else
                    handler(std::move(result).value().attributes.front().result);
            }
        });
}

void ClientService::async_get_list(std::vector<model::Oad> attributes,
                                   session::Session::GetHandler handler) {
    session_->async_get(std::move(attributes), true, std::move(handler));
}

void ClientService::async_get_record(
    protocol::apdu::GetRecord record,
    std::function<void(Result<protocol::apdu::RecordResult>)> handler) {
    session_->async_get_record(
        {std::move(record)}, false,
        [handler = std::move(handler)](Result<protocol::apdu::GetRecordResponse> r) mutable {
            if (handler) {
                if (!r)
                    handler(r.error());
                else
                    handler(std::move(r).value().records.front());
            }
        });
}

void ClientService::async_get_record_list(std::vector<protocol::apdu::GetRecord> records,
                                          session::Session::RecordHandler handler) {
    session_->async_get_record(std::move(records), true, std::move(handler));
}

void ClientService::async_set(model::Oad attribute, model::Data value,
                              std::function<void(Result<std::uint8_t>)> handler) {
    session_->async_set(
        {{attribute, std::move(value)}}, false,
        [handler = std::move(handler)](Result<protocol::apdu::SetResponse> r) mutable {
            if (handler) {
                if (!r)
                    handler(r.error());
                else
                    handler(r.value().attributes.front().dar);
            }
        });
}

void ClientService::async_set_list(std::vector<protocol::apdu::SetAttribute> attributes,
                                   session::Session::SetHandler handler) {
    session_->async_set(std::move(attributes), true, std::move(handler));
}

void ClientService::async_action(model::Omd method, model::Data parameter,
                                 std::function<void(Result<ActionValue>)> handler) {
    session_->async_action(
        {{method, std::move(parameter)}}, false,
        [handler = std::move(handler)](Result<protocol::apdu::ActionResponse> r) mutable {
            if (handler) {
                if (!r)
                    handler(r.error());
                else {
                    auto item = std::move(r).value().methods.front();
                    handler(ActionValue{item.dar, std::move(item.data)});
                }
            }
        });
}

void ClientService::async_action_list(std::vector<protocol::apdu::ActionMethod> methods,
                                      session::Session::ActionHandler handler) {
    session_->async_action(std::move(methods), true, std::move(handler));
}

ServerService::ServerService(std::shared_ptr<session::Session> session,
                             std::shared_ptr<ObjectRegistry> objects)
    : session_(std::move(session)), objects_(std::move(objects)) {
    if (!session_ || !objects_) throw std::invalid_argument("null session/objects");
    session_->set_request_handler([objects = objects_](const protocol::apdu::GetRequest& request) {
        protocol::apdu::GetResponse response;
        response.piid_acd = request.piid;
        response.list = request.list;
        // 每个对象独立返回 DAR/Data，不把列表中的部分失败压缩成整个事务失败。
        for (const auto& attr : request.attributes)
            response.attributes.push_back({attr, objects->read(attr)});
        return response;
    });
    session_->set_record_handler(
        [objects = objects_](const protocol::apdu::GetRecordRequest& request) {
            protocol::apdu::GetRecordResponse response;
            for (const auto& q : request.records)
                response.records.push_back(objects->read_record(q));
            return response;
        });
    session_->set_set_handler([objects = objects_](const protocol::apdu::SetRequest& request) {
        protocol::apdu::SetResponse response;
        for (const auto& item : request.attributes)
            response.attributes.push_back(
                {item.attribute, objects->write(item.attribute, item.value)});
        return response;
    });
    session_->set_action_handler(
        [objects = objects_](const protocol::apdu::ActionRequest& request) {
            protocol::apdu::ActionResponse response;
            for (const auto& item : request.methods) {
                auto value = objects->invoke(item.method, item.parameter);
                response.methods.push_back({item.method, value.dar, std::move(value.data)});
            }
            return response;
        });
}
}  // namespace dlt698::service

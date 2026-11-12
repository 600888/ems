#include <dlt698/app/client.hpp>
#include <dlt698/app/server.hpp>
#include <dlt698/protocol/apdu/security.hpp>
#include <dlt698/standard/records.hpp>

#include "bindings.hpp"

namespace dlt698::python {
void bind_mutation(py::module_& module) {
    Struct<protocol::apdu::SetAttribute> value_SetAttribute(module, "SetAttribute");
    Struct<protocol::apdu::SetResult> value_SetResult(module, "SetResult");
    Struct<protocol::apdu::SetRequest> value_SetRequest(module, "SetRequest");
    Struct<protocol::apdu::SetResponse> value_SetResponse(module, "SetResponse");
    Struct<protocol::apdu::ActionMethod> value_ActionMethod(module, "ActionMethod");
    Struct<protocol::apdu::ActionResult> value_ActionResult(module, "ActionResult");
    Struct<protocol::apdu::ActionRequest> value_ActionRequest(module, "ActionRequest");
    Struct<protocol::apdu::ActionResponse> value_ActionResponse(module, "ActionResponse");
    Struct<protocol::apdu::SetThenGet> value_SetThenGet(module, "SetThenGet");
    Struct<protocol::apdu::ActionThenGet> value_ActionThenGet(module, "ActionThenGet");
    Struct<protocol::apdu::SetThenGetResult> value_SetThenGetResult(module, "SetThenGetResult");
    Struct<protocol::apdu::ActionThenGetResult> value_ActionThenGetResult(module,
                                                                          "ActionThenGetResult");
    Struct<protocol::apdu::SetThenGetRequest> value_SetThenGetRequest(module, "SetThenGetRequest");
    Struct<protocol::apdu::SetThenGetResponse> value_SetThenGetResponse(module,
                                                                        "SetThenGetResponse");
    Struct<protocol::apdu::ActionThenGetRequest> value_ActionThenGetRequest(module,
                                                                            "ActionThenGetRequest");
    Struct<protocol::apdu::ActionThenGetResponse> value_ActionThenGetResponse(
        module, "ActionThenGetResponse");
    value_SetAttribute.field("attribute", &protocol::apdu::SetAttribute::attribute);
    value_SetAttribute.field("value", &protocol::apdu::SetAttribute::value);
    value_SetAttribute.finish();
    value_SetResult.field("attribute", &protocol::apdu::SetResult::attribute);
    value_SetResult.field("dar", &protocol::apdu::SetResult::dar);
    value_SetResult.finish();
    value_SetRequest.field("piid", &protocol::apdu::SetRequest::piid);
    value_SetRequest.field("list", &protocol::apdu::SetRequest::list);
    value_SetRequest.field("attributes", &protocol::apdu::SetRequest::attributes);
    value_SetRequest.field("time_tag", &protocol::apdu::SetRequest::time_tag);
    value_SetRequest.finish();
    value_SetResponse.field("piid_acd", &protocol::apdu::SetResponse::piid_acd);
    value_SetResponse.field("list", &protocol::apdu::SetResponse::list);
    value_SetResponse.field("attributes", &protocol::apdu::SetResponse::attributes);
    value_SetResponse.field("time_tag", &protocol::apdu::SetResponse::time_tag);
    value_SetResponse.field("follow_report", &protocol::apdu::SetResponse::follow_report);
    value_SetResponse.finish();
    value_ActionMethod.field("method", &protocol::apdu::ActionMethod::method);
    value_ActionMethod.field("parameter", &protocol::apdu::ActionMethod::parameter);
    value_ActionMethod.finish();
    value_ActionResult.field("method", &protocol::apdu::ActionResult::method);
    value_ActionResult.field("dar", &protocol::apdu::ActionResult::dar);
    value_ActionResult.field("data", &protocol::apdu::ActionResult::data);
    value_ActionResult.finish();
    value_ActionRequest.field("piid", &protocol::apdu::ActionRequest::piid);
    value_ActionRequest.field("list", &protocol::apdu::ActionRequest::list);
    value_ActionRequest.field("methods", &protocol::apdu::ActionRequest::methods);
    value_ActionRequest.field("time_tag", &protocol::apdu::ActionRequest::time_tag);
    value_ActionRequest.finish();
    value_ActionResponse.field("piid_acd", &protocol::apdu::ActionResponse::piid_acd);
    value_ActionResponse.field("list", &protocol::apdu::ActionResponse::list);
    value_ActionResponse.field("methods", &protocol::apdu::ActionResponse::methods);
    value_ActionResponse.field("time_tag", &protocol::apdu::ActionResponse::time_tag);
    value_ActionResponse.field("follow_report", &protocol::apdu::ActionResponse::follow_report);
    value_ActionResponse.finish();
    value_SetThenGet.field("set", &protocol::apdu::SetThenGet::set);
    value_SetThenGet.field("read", &protocol::apdu::SetThenGet::read);
    value_SetThenGet.field("delay_seconds", &protocol::apdu::SetThenGet::delay_seconds);
    value_SetThenGet.finish();
    value_ActionThenGet.field("action", &protocol::apdu::ActionThenGet::action);
    value_ActionThenGet.field("read", &protocol::apdu::ActionThenGet::read);
    value_ActionThenGet.field("delay_seconds", &protocol::apdu::ActionThenGet::delay_seconds);
    value_ActionThenGet.finish();
    value_SetThenGetResult.field("set", &protocol::apdu::SetThenGetResult::set);
    value_SetThenGetResult.field("read", &protocol::apdu::SetThenGetResult::read);
    value_SetThenGetResult.finish();
    value_ActionThenGetResult.field("action", &protocol::apdu::ActionThenGetResult::action);
    value_ActionThenGetResult.field("read", &protocol::apdu::ActionThenGetResult::read);
    value_ActionThenGetResult.finish();
    value_SetThenGetRequest.field("piid", &protocol::apdu::SetThenGetRequest::piid);
    value_SetThenGetRequest.field("items", &protocol::apdu::SetThenGetRequest::items);
    value_SetThenGetRequest.field("time_tag", &protocol::apdu::SetThenGetRequest::time_tag);
    value_SetThenGetRequest.finish();
    value_SetThenGetResponse.field("piid_acd", &protocol::apdu::SetThenGetResponse::piid_acd);
    value_SetThenGetResponse.field("items", &protocol::apdu::SetThenGetResponse::items);
    value_SetThenGetResponse.field("time_tag", &protocol::apdu::SetThenGetResponse::time_tag);
    value_SetThenGetResponse.field("follow_report",
                                   &protocol::apdu::SetThenGetResponse::follow_report);
    value_SetThenGetResponse.finish();
    value_ActionThenGetRequest.field("piid", &protocol::apdu::ActionThenGetRequest::piid);
    value_ActionThenGetRequest.field("items", &protocol::apdu::ActionThenGetRequest::items);
    value_ActionThenGetRequest.field("time_tag", &protocol::apdu::ActionThenGetRequest::time_tag);
    value_ActionThenGetRequest.finish();
    value_ActionThenGetResponse.field("piid_acd", &protocol::apdu::ActionThenGetResponse::piid_acd);
    value_ActionThenGetResponse.field("items", &protocol::apdu::ActionThenGetResponse::items);
    value_ActionThenGetResponse.field("time_tag", &protocol::apdu::ActionThenGetResponse::time_tag);
    value_ActionThenGetResponse.field("follow_report",
                                      &protocol::apdu::ActionThenGetResponse::follow_report);
    value_ActionThenGetResponse.finish();
}
}  // namespace dlt698::python

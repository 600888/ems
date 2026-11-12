#include <dlt698/app/client.hpp>
#include <dlt698/app/server.hpp>
#include <dlt698/protocol/apdu/security.hpp>
#include <dlt698/standard/records.hpp>

#include "bindings.hpp"

namespace dlt698::python {
void bind_messages(py::module_& module) {
    Struct<protocol::apdu::TimeTag> value_TimeTag(module, "TimeTag");
    Struct<protocol::apdu::GetRequest> value_GetRequest(module, "GetRequest");
    Struct<protocol::apdu::AttributeResult> value_AttributeResult(module, "AttributeResult");
    Struct<protocol::apdu::GetRecord> value_GetRecord(module, "GetRecord");
    Struct<protocol::apdu::GetRecordRequest> value_GetRecordRequest(module, "GetRecordRequest");
    Struct<protocol::apdu::RecordResult> value_RecordResult(module, "RecordResult");
    Struct<protocol::apdu::GetResponse> value_GetResponse(module, "GetResponse");
    Struct<protocol::apdu::GetRecordResponse> value_GetRecordResponse(module, "GetRecordResponse");
    Struct<protocol::apdu::GetNextRequest> value_GetNextRequest(module, "GetNextRequest");
    Struct<protocol::apdu::GetNextResponse> value_GetNextResponse(module, "GetNextResponse");
    Struct<protocol::apdu::GetMd5Request> value_GetMd5Request(module, "GetMd5Request");
    Struct<protocol::apdu::GetMd5Response> value_GetMd5Response(module, "GetMd5Response");
    value_TimeTag.field("sent_at", &protocol::apdu::TimeTag::sent_at);
    value_TimeTag.field("allowed_delay", &protocol::apdu::TimeTag::allowed_delay);
    value_TimeTag.finish();
    value_GetRequest.field("piid", &protocol::apdu::GetRequest::piid);
    value_GetRequest.field("list", &protocol::apdu::GetRequest::list);
    value_GetRequest.field("attributes", &protocol::apdu::GetRequest::attributes);
    value_GetRequest.field("time_tag", &protocol::apdu::GetRequest::time_tag);
    value_GetRequest.finish();
    value_AttributeResult.field("attribute", &protocol::apdu::AttributeResult::attribute);
    value_AttributeResult.field("result", &protocol::apdu::AttributeResult::result);
    value_AttributeResult.finish();
    value_GetRecord.field("attribute", &protocol::apdu::GetRecord::attribute);
    value_GetRecord.field("rows", &protocol::apdu::GetRecord::rows);
    value_GetRecord.field("columns", &protocol::apdu::GetRecord::columns);
    value_GetRecord.finish();
    value_GetRecordRequest.field("piid", &protocol::apdu::GetRecordRequest::piid);
    value_GetRecordRequest.field("list", &protocol::apdu::GetRecordRequest::list);
    value_GetRecordRequest.field("records", &protocol::apdu::GetRecordRequest::records);
    value_GetRecordRequest.field("time_tag", &protocol::apdu::GetRecordRequest::time_tag);
    value_GetRecordRequest.finish();
    value_RecordResult.field("attribute", &protocol::apdu::RecordResult::attribute);
    value_RecordResult.field("columns", &protocol::apdu::RecordResult::columns);
    value_RecordResult.field("result", &protocol::apdu::RecordResult::result);
    value_RecordResult.finish();
    value_GetResponse.field("piid_acd", &protocol::apdu::GetResponse::piid_acd);
    value_GetResponse.field("list", &protocol::apdu::GetResponse::list);
    value_GetResponse.field("attributes", &protocol::apdu::GetResponse::attributes);
    value_GetResponse.field("time_tag", &protocol::apdu::GetResponse::time_tag);
    value_GetResponse.field("follow_report", &protocol::apdu::GetResponse::follow_report);
    value_GetResponse.finish();
    value_GetRecordResponse.field("piid_acd", &protocol::apdu::GetRecordResponse::piid_acd);
    value_GetRecordResponse.field("list", &protocol::apdu::GetRecordResponse::list);
    value_GetRecordResponse.field("records", &protocol::apdu::GetRecordResponse::records);
    value_GetRecordResponse.field("time_tag", &protocol::apdu::GetRecordResponse::time_tag);
    value_GetRecordResponse.field("follow_report",
                                  &protocol::apdu::GetRecordResponse::follow_report);
    value_GetRecordResponse.finish();
    value_GetNextRequest.field("piid", &protocol::apdu::GetNextRequest::piid);
    value_GetNextRequest.field("block", &protocol::apdu::GetNextRequest::block);
    value_GetNextRequest.field("time_tag", &protocol::apdu::GetNextRequest::time_tag);
    value_GetNextRequest.finish();
    value_GetNextResponse.field("piid_acd", &protocol::apdu::GetNextResponse::piid_acd);
    value_GetNextResponse.field("last", &protocol::apdu::GetNextResponse::last);
    value_GetNextResponse.field("block", &protocol::apdu::GetNextResponse::block);
    value_GetNextResponse.field("result", &protocol::apdu::GetNextResponse::result);
    value_GetNextResponse.field("time_tag", &protocol::apdu::GetNextResponse::time_tag);
    value_GetNextResponse.field("follow_report", &protocol::apdu::GetNextResponse::follow_report);
    value_GetNextResponse.finish();
    value_GetMd5Request.field("piid", &protocol::apdu::GetMd5Request::piid);
    value_GetMd5Request.field("attribute", &protocol::apdu::GetMd5Request::attribute);
    value_GetMd5Request.field("time_tag", &protocol::apdu::GetMd5Request::time_tag);
    value_GetMd5Request.finish();
    value_GetMd5Response.field("piid_acd", &protocol::apdu::GetMd5Response::piid_acd);
    value_GetMd5Response.field("attribute", &protocol::apdu::GetMd5Response::attribute);
    value_GetMd5Response.field("result", &protocol::apdu::GetMd5Response::result);
    value_GetMd5Response.field("time_tag", &protocol::apdu::GetMd5Response::time_tag);
    value_GetMd5Response.field("follow_report", &protocol::apdu::GetMd5Response::follow_report);
    value_GetMd5Response.finish();
}
}  // namespace dlt698::python

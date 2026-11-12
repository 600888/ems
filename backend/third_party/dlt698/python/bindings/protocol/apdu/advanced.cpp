#include <dlt698/app/client.hpp>
#include <dlt698/app/server.hpp>
#include <dlt698/protocol/apdu/security.hpp>
#include <dlt698/standard/records.hpp>

#include "bindings.hpp"

namespace dlt698::python {
void bind_advanced(py::module_& module) {
    Struct<protocol::apdu::TransData> value_TransData(module, "TransData");
    Struct<protocol::apdu::ReportNotification> value_ReportNotification(module,
                                                                        "ReportNotification");
    Struct<protocol::apdu::ReportResponse> value_ReportResponse(module, "ReportResponse");
    Struct<protocol::apdu::ProxyRecordRequest> value_ProxyRecordRequest(module,
                                                                        "ProxyRecordRequest");
    Struct<protocol::apdu::ProxyRecordResponse> value_ProxyRecordResponse(module,
                                                                          "ProxyRecordResponse");
    Struct<protocol::apdu::ProxyTransRequest> value_ProxyTransRequest(module, "ProxyTransRequest");
    Struct<protocol::apdu::ProxyTransResponse> value_ProxyTransResponse(module,
                                                                        "ProxyTransResponse");
    Struct<protocol::apdu::ProxyRequest> value_ProxyRequest(module, "ProxyRequest");
    Struct<protocol::apdu::ProxyResponse> value_ProxyResponse(module, "ProxyResponse");
    Struct<protocol::apdu::RnMac> value_RnMac(module, "RnMac");
    Struct<protocol::apdu::SecurityRequest> value_SecurityRequest(module, "SecurityRequest");
    Struct<protocol::apdu::SecurityResponse> value_SecurityResponse(module, "SecurityResponse");
    {
        using T = protocol::apdu::ProxyTarget<model::Oad>;
        Struct<T> value(module, "ProxyGetTarget");
        value.field("server", &T::server)
            .field("timeout_seconds", &T::timeout_seconds)
            .field("items", &T::items)
            .finish();
    }
    {
        using T = protocol::apdu::ProxyTarget<protocol::apdu::AttributeResult>;
        Struct<T> value(module, "ProxyGetResultTarget");
        value.field("server", &T::server)
            .field("timeout_seconds", &T::timeout_seconds)
            .field("items", &T::items)
            .finish();
    }
    {
        using T = protocol::apdu::ProxyTarget<protocol::apdu::SetAttribute>;
        Struct<T> value(module, "ProxySetTarget");
        value.field("server", &T::server)
            .field("timeout_seconds", &T::timeout_seconds)
            .field("items", &T::items)
            .finish();
    }
    {
        using T = protocol::apdu::ProxyTarget<protocol::apdu::SetResult>;
        Struct<T> value(module, "ProxySetResultTarget");
        value.field("server", &T::server)
            .field("timeout_seconds", &T::timeout_seconds)
            .field("items", &T::items)
            .finish();
    }
    {
        using T = protocol::apdu::ProxyTarget<protocol::apdu::ActionMethod>;
        Struct<T> value(module, "ProxyActionTarget");
        value.field("server", &T::server)
            .field("timeout_seconds", &T::timeout_seconds)
            .field("items", &T::items)
            .finish();
    }
    {
        using T = protocol::apdu::ProxyTarget<protocol::apdu::ActionResult>;
        Struct<T> value(module, "ProxyActionResultTarget");
        value.field("server", &T::server)
            .field("timeout_seconds", &T::timeout_seconds)
            .field("items", &T::items)
            .finish();
    }
    {
        using T = protocol::apdu::ProxyTarget<protocol::apdu::SetThenGet>;
        Struct<T> value(module, "ProxySetThenGetTarget");
        value.field("server", &T::server)
            .field("timeout_seconds", &T::timeout_seconds)
            .field("items", &T::items)
            .finish();
    }
    {
        using T = protocol::apdu::ProxyTarget<protocol::apdu::SetThenGetResult>;
        Struct<T> value(module, "ProxySetThenGetResultTarget");
        value.field("server", &T::server)
            .field("timeout_seconds", &T::timeout_seconds)
            .field("items", &T::items)
            .finish();
    }
    {
        using T = protocol::apdu::ProxyTarget<protocol::apdu::ActionThenGet>;
        Struct<T> value(module, "ProxyActionThenGetTarget");
        value.field("server", &T::server)
            .field("timeout_seconds", &T::timeout_seconds)
            .field("items", &T::items)
            .finish();
    }
    {
        using T = protocol::apdu::ProxyTarget<protocol::apdu::ActionThenGetResult>;
        Struct<T> value(module, "ProxyActionThenGetResultTarget");
        value.field("server", &T::server)
            .field("timeout_seconds", &T::timeout_seconds)
            .field("items", &T::items)
            .finish();
    }
    value_TransData.field("port", &protocol::apdu::TransData::port);
    value_TransData.field("data", &protocol::apdu::TransData::data);
    value_TransData.finish();
    value_ReportNotification.field("piid_acd", &protocol::apdu::ReportNotification::piid_acd);
    value_ReportNotification.field("payload", &protocol::apdu::ReportNotification::payload);
    value_ReportNotification.field("time_tag", &protocol::apdu::ReportNotification::time_tag);
    value_ReportNotification.field("follow_report",
                                   &protocol::apdu::ReportNotification::follow_report);
    value_ReportNotification.finish();
    value_ReportResponse.field("piid", &protocol::apdu::ReportResponse::piid);
    value_ReportResponse.field("choice", &protocol::apdu::ReportResponse::choice);
    value_ReportResponse.field("attributes", &protocol::apdu::ReportResponse::attributes);
    value_ReportResponse.field("time_tag", &protocol::apdu::ReportResponse::time_tag);
    value_ReportResponse.finish();
    value_ProxyRecordRequest.field("server", &protocol::apdu::ProxyRecordRequest::server);
    value_ProxyRecordRequest.field("record", &protocol::apdu::ProxyRecordRequest::record);
    value_ProxyRecordRequest.finish();
    value_ProxyRecordResponse.field("server", &protocol::apdu::ProxyRecordResponse::server);
    value_ProxyRecordResponse.field("record", &protocol::apdu::ProxyRecordResponse::record);
    value_ProxyRecordResponse.finish();
    value_ProxyTransRequest.field("port", &protocol::apdu::ProxyTransRequest::port);
    value_ProxyTransRequest.field("communication",
                                  &protocol::apdu::ProxyTransRequest::communication);
    value_ProxyTransRequest.field("response_timeout_seconds",
                                  &protocol::apdu::ProxyTransRequest::response_timeout_seconds);
    value_ProxyTransRequest.field("byte_timeout_milliseconds",
                                  &protocol::apdu::ProxyTransRequest::byte_timeout_milliseconds);
    value_ProxyTransRequest.field("command", &protocol::apdu::ProxyTransRequest::command);
    value_ProxyTransRequest.finish();
    value_ProxyTransResponse.field("port", &protocol::apdu::ProxyTransResponse::port);
    value_ProxyTransResponse.field("result", &protocol::apdu::ProxyTransResponse::result);
    value_ProxyTransResponse.finish();
    value_ProxyRequest.field("piid", &protocol::apdu::ProxyRequest::piid);
    value_ProxyRequest.field("timeout_seconds", &protocol::apdu::ProxyRequest::timeout_seconds);
    value_ProxyRequest.field("payload", &protocol::apdu::ProxyRequest::payload);
    value_ProxyRequest.field("time_tag", &protocol::apdu::ProxyRequest::time_tag);
    value_ProxyRequest.finish();
    value_ProxyResponse.field("piid_acd", &protocol::apdu::ProxyResponse::piid_acd);
    value_ProxyResponse.field("payload", &protocol::apdu::ProxyResponse::payload);
    value_ProxyResponse.field("time_tag", &protocol::apdu::ProxyResponse::time_tag);
    value_ProxyResponse.field("follow_report", &protocol::apdu::ProxyResponse::follow_report);
    value_ProxyResponse.finish();
    value_RnMac.field("random", &protocol::apdu::RnMac::random);
    value_RnMac.field("mac", &protocol::apdu::RnMac::mac);
    value_RnMac.finish();
    value_SecurityRequest.field("encrypted", &protocol::apdu::SecurityRequest::encrypted);
    value_SecurityRequest.field("application", &protocol::apdu::SecurityRequest::application);
    value_SecurityRequest.field("verification", &protocol::apdu::SecurityRequest::verification);
    value_SecurityRequest.finish();
    value_SecurityResponse.field("encrypted", &protocol::apdu::SecurityResponse::encrypted);
    value_SecurityResponse.field("application", &protocol::apdu::SecurityResponse::application);
    value_SecurityResponse.field("verification", &protocol::apdu::SecurityResponse::verification);
    value_SecurityResponse.finish();
}
}  // namespace dlt698::python

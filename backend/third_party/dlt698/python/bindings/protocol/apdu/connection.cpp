#include <dlt698/app/client.hpp>
#include <dlt698/app/server.hpp>
#include <dlt698/protocol/apdu/security.hpp>
#include <dlt698/standard/records.hpp>

#include "bindings.hpp"

namespace dlt698::python {
void bind_connection(py::module_& module) {
    Struct<protocol::apdu::AssociationParameters> value_AssociationParameters(
        module, "AssociationParameters");
    Struct<protocol::apdu::FactoryVersion> value_FactoryVersion(module, "FactoryVersion");
    Struct<protocol::apdu::NullSecurity> value_NullSecurity(module, "NullSecurity");
    Struct<protocol::apdu::PasswordSecurity> value_PasswordSecurity(module, "PasswordSecurity");
    Struct<protocol::apdu::SymmetrySecurity> value_SymmetrySecurity(module, "SymmetrySecurity");
    Struct<protocol::apdu::SignatureSecurity> value_SignatureSecurity(module, "SignatureSecurity");
    Struct<protocol::apdu::SecurityData> value_SecurityData(module, "SecurityData");
    Struct<protocol::apdu::LinkRequest> value_LinkRequest(module, "LinkRequest");
    Struct<protocol::apdu::LinkResponse> value_LinkResponse(module, "LinkResponse");
    Struct<protocol::apdu::ConnectRequest> value_ConnectRequest(module, "ConnectRequest");
    Struct<protocol::apdu::ConnectResponse> value_ConnectResponse(module, "ConnectResponse");
    Struct<protocol::apdu::ReleaseRequest> value_ReleaseRequest(module, "ReleaseRequest");
    Struct<protocol::apdu::ReleaseResponse> value_ReleaseResponse(module, "ReleaseResponse");
    Struct<protocol::apdu::ReleaseNotification> value_ReleaseNotification(module,
                                                                          "ReleaseNotification");
    Struct<protocol::apdu::ErrorResponse> value_ErrorResponse(module, "ErrorResponse");
    value_AssociationParameters.field("version", &protocol::apdu::AssociationParameters::version);
    value_AssociationParameters.field("protocol", &protocol::apdu::AssociationParameters::protocol);
    value_AssociationParameters.field("function", &protocol::apdu::AssociationParameters::function);
    value_AssociationParameters.field("send_frame_bytes",
                                      &protocol::apdu::AssociationParameters::send_frame_bytes);
    value_AssociationParameters.field("receive_frame_bytes",
                                      &protocol::apdu::AssociationParameters::receive_frame_bytes);
    value_AssociationParameters.field("receive_window",
                                      &protocol::apdu::AssociationParameters::receive_window);
    value_AssociationParameters.field("apdu_bytes",
                                      &protocol::apdu::AssociationParameters::apdu_bytes);
    value_AssociationParameters.field("timeout_seconds",
                                      &protocol::apdu::AssociationParameters::timeout_seconds);
    value_AssociationParameters.finish();
    value_FactoryVersion.field("manufacturer", &protocol::apdu::FactoryVersion::manufacturer);
    value_FactoryVersion.field("software_version",
                               &protocol::apdu::FactoryVersion::software_version);
    value_FactoryVersion.field("software_date", &protocol::apdu::FactoryVersion::software_date);
    value_FactoryVersion.field("hardware_version",
                               &protocol::apdu::FactoryVersion::hardware_version);
    value_FactoryVersion.field("hardware_date", &protocol::apdu::FactoryVersion::hardware_date);
    value_FactoryVersion.field("extension", &protocol::apdu::FactoryVersion::extension);
    value_FactoryVersion.finish();
    value_NullSecurity.finish();
    value_PasswordSecurity.field("password", &protocol::apdu::PasswordSecurity::password);
    value_PasswordSecurity.finish();
    value_SymmetrySecurity.field("ciphertext", &protocol::apdu::SymmetrySecurity::ciphertext);
    value_SymmetrySecurity.field("signature", &protocol::apdu::SymmetrySecurity::signature);
    value_SymmetrySecurity.finish();
    value_SignatureSecurity.field("ciphertext", &protocol::apdu::SignatureSecurity::ciphertext);
    value_SignatureSecurity.field("signature", &protocol::apdu::SignatureSecurity::signature);
    value_SignatureSecurity.finish();
    value_SecurityData.field("random", &protocol::apdu::SecurityData::random);
    value_SecurityData.field("signature", &protocol::apdu::SecurityData::signature);
    value_SecurityData.finish();
    value_LinkRequest.field("piid_acd", &protocol::apdu::LinkRequest::piid_acd);
    value_LinkRequest.field("type", &protocol::apdu::LinkRequest::type);
    value_LinkRequest.field("heartbeat_seconds", &protocol::apdu::LinkRequest::heartbeat_seconds);
    value_LinkRequest.field("requested_at", &protocol::apdu::LinkRequest::requested_at);
    value_LinkRequest.finish();
    value_LinkResponse.field("piid", &protocol::apdu::LinkResponse::piid);
    value_LinkResponse.field("result", &protocol::apdu::LinkResponse::result);
    value_LinkResponse.field("requested_at", &protocol::apdu::LinkResponse::requested_at);
    value_LinkResponse.field("received_at", &protocol::apdu::LinkResponse::received_at);
    value_LinkResponse.field("responded_at", &protocol::apdu::LinkResponse::responded_at);
    value_LinkResponse.finish();
    value_ConnectRequest.field("piid", &protocol::apdu::ConnectRequest::piid);
    value_ConnectRequest.field("parameters", &protocol::apdu::ConnectRequest::parameters);
    value_ConnectRequest.field("mechanism", &protocol::apdu::ConnectRequest::mechanism);
    value_ConnectRequest.field("time_tag", &protocol::apdu::ConnectRequest::time_tag);
    value_ConnectRequest.finish();
    value_ConnectResponse.field("piid_acd", &protocol::apdu::ConnectResponse::piid_acd);
    value_ConnectResponse.field("factory", &protocol::apdu::ConnectResponse::factory);
    value_ConnectResponse.field("parameters", &protocol::apdu::ConnectResponse::parameters);
    value_ConnectResponse.field("result", &protocol::apdu::ConnectResponse::result);
    value_ConnectResponse.field("security", &protocol::apdu::ConnectResponse::security);
    value_ConnectResponse.field("time_tag", &protocol::apdu::ConnectResponse::time_tag);
    value_ConnectResponse.field("follow_report", &protocol::apdu::ConnectResponse::follow_report);
    value_ConnectResponse.finish();
    value_ReleaseRequest.field("piid", &protocol::apdu::ReleaseRequest::piid);
    value_ReleaseRequest.field("time_tag", &protocol::apdu::ReleaseRequest::time_tag);
    value_ReleaseRequest.finish();
    value_ReleaseResponse.field("piid_acd", &protocol::apdu::ReleaseResponse::piid_acd);
    value_ReleaseResponse.field("result", &protocol::apdu::ReleaseResponse::result);
    value_ReleaseResponse.field("time_tag", &protocol::apdu::ReleaseResponse::time_tag);
    value_ReleaseResponse.field("follow_report", &protocol::apdu::ReleaseResponse::follow_report);
    value_ReleaseResponse.finish();
    value_ReleaseNotification.field("piid_acd", &protocol::apdu::ReleaseNotification::piid_acd);
    value_ReleaseNotification.field("established_at",
                                    &protocol::apdu::ReleaseNotification::established_at);
    value_ReleaseNotification.field("current_time",
                                    &protocol::apdu::ReleaseNotification::current_time);
    value_ReleaseNotification.field("time_tag", &protocol::apdu::ReleaseNotification::time_tag);
    value_ReleaseNotification.field("follow_report",
                                    &protocol::apdu::ReleaseNotification::follow_report);
    value_ReleaseNotification.finish();
    value_ErrorResponse.field("server", &protocol::apdu::ErrorResponse::server);
    value_ErrorResponse.field("piid", &protocol::apdu::ErrorResponse::piid);
    value_ErrorResponse.field("type", &protocol::apdu::ErrorResponse::type);
    value_ErrorResponse.field("time_tag", &protocol::apdu::ErrorResponse::time_tag);
    value_ErrorResponse.field("follow_report", &protocol::apdu::ErrorResponse::follow_report);
    value_ErrorResponse.finish();
}
}  // namespace dlt698::python

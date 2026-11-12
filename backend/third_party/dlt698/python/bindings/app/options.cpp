#include <dlt698/app/client.hpp>
#include <dlt698/app/server.hpp>
#include <dlt698/protocol/apdu/security.hpp>
#include <dlt698/standard/records.hpp>

#include "bindings.hpp"
#include "options_callbacks.hpp"

namespace dlt698::python {
void bind_options(py::module_& module) {
    Struct<transport::ChannelOptions> value_ChannelOptions(module, "ChannelOptions");
    Struct<transport::SerialOptions> value_SerialOptions(module, "SerialOptions");
    Struct<transport::SerialLinkOptions> value_SerialLinkOptions(module, "SerialLinkOptions");
    Struct<protocol::link::ServerAddress> value_ServerAddress(module, "ServerAddress");
    Struct<protocol::link::Frame> value_Frame(module, "Frame");
    Struct<session::SessionOptions> value_SessionOptions(module, "SessionOptions");
    Struct<app::ClientOptions> value_ClientOptions(module, "ClientOptions");
    Struct<app::ServerOptions> value_ServerOptions(module, "ServerOptions");
    Struct<standard::DeviceLayout> value_DeviceLayout(module, "DeviceLayout");
    Struct<service::DeviceOptions> value_DeviceOptions(module, "DeviceOptions");
    Struct<service::AttributeSchema> value_AttributeSchema(module, "AttributeSchema");
    Struct<service::MethodSchema> value_MethodSchema(module, "MethodSchema");
    Struct<service::ObjectSchema> value_ObjectSchema(module, "ObjectSchema");
    Struct<service::ActionValue> value_ActionValue(module, "ActionValue");
    value_ChannelOptions.field("read_chunk_bytes", &transport::ChannelOptions::read_chunk_bytes);
    value_ChannelOptions.field("max_pending_write_bytes",
                               &transport::ChannelOptions::max_pending_write_bytes);
    value_ChannelOptions.field("max_pending_writes",
                               &transport::ChannelOptions::max_pending_writes);
    value_ChannelOptions.finish();
    value_SerialOptions.field("baud_rate", &transport::SerialOptions::baud_rate);
    value_SerialOptions.field("data_bits", &transport::SerialOptions::data_bits);
    value_SerialOptions.field("parity", &transport::SerialOptions::parity);
    value_SerialOptions.field("stop_bits", &transport::SerialOptions::stop_bits);
    value_SerialOptions.field("flow_control", &transport::SerialOptions::flow_control);
    value_SerialOptions.field("channel", &transport::SerialOptions::channel);
    value_SerialOptions.finish();
    value_SerialLinkOptions.field("baud_rate", &transport::SerialLinkOptions::baud_rate);
    value_SerialLinkOptions.field("bits_per_character",
                                  &transport::SerialLinkOptions::bits_per_character);
    value_SerialLinkOptions.field("max_pending_write_bytes",
                                  &transport::SerialLinkOptions::max_pending_write_bytes);
    value_SerialLinkOptions.field("max_pending_writes",
                                  &transport::SerialLinkOptions::max_pending_writes);
    serial_callbacks(value_SerialLinkOptions);
    value_SerialLinkOptions.finish();
    value_ServerAddress.field("type", &protocol::link::ServerAddress::type);
    value_ServerAddress.field("logical", &protocol::link::ServerAddress::logical);
    value_ServerAddress.field("bytes", &protocol::link::ServerAddress::bytes);
    value_ServerAddress.finish();
    value_Frame.field("control", &protocol::link::Frame::control);
    value_Frame.field("server", &protocol::link::Frame::server);
    value_Frame.field("client", &protocol::link::Frame::client);
    value_Frame.field("payload", &protocol::link::Frame::payload);
    value_Frame.finish();
    value_SessionOptions.field("role", &session::SessionOptions::role);
    value_SessionOptions.field("server", &session::SessionOptions::server);
    value_SessionOptions.field("client_address", &session::SessionOptions::client_address);
    value_SessionOptions.field("limits", &session::SessionOptions::limits);
    value_SessionOptions.field("parameters", &session::SessionOptions::parameters);
    value_SessionOptions.field("factory", &session::SessionOptions::factory);
    value_SessionOptions.field("security_backend", &session::SessionOptions::security_backend);
    value_SessionOptions.field("security_backend_factory",
                               &session::SessionOptions::security_backend_factory);
    value_SessionOptions.field("protect_application",
                               &session::SessionOptions::protect_application);
    value_SessionOptions.field("request_timeout", &session::SessionOptions::request_timeout);
    value_SessionOptions.field("id_reuse_delay", &session::SessionOptions::id_reuse_delay);
    value_SessionOptions.field("require_login", &session::SessionOptions::require_login);
    value_SessionOptions.field("preset_association", &session::SessionOptions::preset_association);
    value_SessionOptions.field("clock_trusted", &session::SessionOptions::clock_trusted);
    value_SessionOptions.field("heartbeat_seconds", &session::SessionOptions::heartbeat_seconds);
    value_SessionOptions.field("request_time_tag", &session::SessionOptions::request_time_tag);
    value_SessionOptions.field("fragment_timeout", &session::SessionOptions::fragment_timeout);
    value_SessionOptions.field("reassembly_timeout", &session::SessionOptions::reassembly_timeout);
    value_SessionOptions.field("fragment_retries", &session::SessionOptions::fragment_retries);
    value_SessionOptions.field("report_retries", &session::SessionOptions::report_retries);
    value_SessionOptions.field("prefer_get_blocks", &session::SessionOptions::prefer_get_blocks);
    value_SessionOptions.field("calendar_clock", &session::SessionOptions::calendar_clock);
    value_SessionOptions.finish();
    value_ClientOptions.field("protocol", &app::ClientOptions::protocol);
    value_ClientOptions.field("transport_timeout", &app::ClientOptions::transport_timeout);
    value_ClientOptions.field("login_timeout", &app::ClientOptions::login_timeout);
    value_ClientOptions.field("channel", &app::ClientOptions::channel);
    value_ClientOptions.finish();
    value_ServerOptions.field("protocol", &app::ServerOptions::protocol);
    value_ServerOptions.field("max_connections", &app::ServerOptions::max_connections);
    value_ServerOptions.field("heartbeat_seconds", &app::ServerOptions::heartbeat_seconds);
    value_ServerOptions.field("channel", &app::ServerOptions::channel);
    value_ServerOptions.finish();
    value_DeviceLayout.field("wiring", &standard::DeviceLayout::wiring);
    value_DeviceLayout.field("tariff_count", &standard::DeviceLayout::tariff_count);
    value_DeviceLayout.field("harmonic_order", &standard::DeviceLayout::harmonic_order);
    value_DeviceLayout.finish();
    value_DeviceOptions.field("layout", &service::DeviceOptions::layout);
    value_DeviceOptions.field("limits", &service::DeviceOptions::limits);
    value_DeviceOptions.field("max_objects", &service::DeviceOptions::max_objects);
    value_DeviceOptions.field("max_attributes", &service::DeviceOptions::max_attributes);
    value_DeviceOptions.field("max_value_bytes", &service::DeviceOptions::max_value_bytes);
    value_DeviceOptions.finish();
    value_AttributeSchema.field("number", &service::AttributeSchema::number);
    value_AttributeSchema.field("type", &service::AttributeSchema::type);
    value_AttributeSchema.field("readable", &service::AttributeSchema::readable);
    value_AttributeSchema.field("writable", &service::AttributeSchema::writable);
    value_AttributeSchema.field("record", &service::AttributeSchema::record);
    value_AttributeSchema.finish();
    value_MethodSchema.field("number", &service::MethodSchema::number);
    value_MethodSchema.field("parameter_type", &service::MethodSchema::parameter_type);
    value_MethodSchema.field("return_type", &service::MethodSchema::return_type);
    value_MethodSchema.field("executable", &service::MethodSchema::executable);
    value_MethodSchema.finish();
    value_ObjectSchema.field("oi", &service::ObjectSchema::oi);
    value_ObjectSchema.field("name", &service::ObjectSchema::name);
    value_ObjectSchema.field("attributes", &service::ObjectSchema::attributes);
    value_ObjectSchema.field("methods", &service::ObjectSchema::methods);
    value_ObjectSchema.finish();
    value_ActionValue.field("dar", &service::ActionValue::dar);
    value_ActionValue.field("data", &service::ActionValue::data);
    value_ActionValue.finish();
}
}  // namespace dlt698::python

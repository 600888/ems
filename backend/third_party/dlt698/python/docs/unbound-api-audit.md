# 全部未直接映射的 C++ 公开声明

2026-10-08。基于已核对的 `python/api-map.json`，覆盖 43 个头文件、1300 条 AST 公开声明。
其中 301 条标为 deferred；该标签表示未直接映射，不表示同等数量的未实现功能。
真实实现与绑定缺口的判断见 [主报告](implementation-audit.md)。

本次清单 SHA-256：`d8b80a895612b0a3742a261f33ad178f6eade806f625abb7cb734f09919d9d68`。后续源码/清单变化须重新核对。

## 分类概览

| 头文件 | 声明数 | 审计判断 |
| --- | ---: | --- |
| [app/client.hpp](../cpp/include/dlt698/app/client.hpp) | 2 | Client 已通过 NativeClient 和 app.Client 包装；diagnostic/traffic 用拥有型 EventQueue/on_event 适配，未直接暴露后台线程 callable。 |
| [app/server.hpp](../cpp/include/dlt698/app/server.hpp) | 2 | Server 已通过 NativeServer 和 app.Server/AsyncServer 包装；diagnostic/traffic 用拥有型 EventQueue/on_event 适配。 |
| [codec/data_codec.hpp](../cpp/include/dlt698/codec/data_codec.hpp) | 6 | 这些函数依赖 C++ Reader/Writer，未公开独立增量接口；完整 encode_data/decode_data 已实现。 |
| [codec/record_codec.hpp](../cpp/include/dlt698/codec/record_codec.hpp) | 6 | 这些函数依赖 C++ Reader/Writer，未公开独立增量接口；记录描述符模型、Data 工厂和 APDU codec 已实现。 |
| [common/bytes.hpp](../cpp/include/dlt698/common/bytes.hpp) | 24 | ByteView/Bytes 用 bytes 适配，DecodeFailure 用 Python 异常适配；Reader/Writer 没有独立 Python 增量接口；from_hex/to_hex 未绑定，可用标准库。 |
| [common/executor.hpp](../cpp/include/dlt698/common/executor.hpp) | 2 | Clock/Task 类型别名用秒数和 Python callable 适配，执行器本身已绑定。 |
| [common/md5.hpp](../cpp/include/dlt698/common/md5.hpp) | 1 | 独立原生 md5 未绑定；服务端 GET MD5 已在 AdvancedService 实现，也可用 Python hashlib。 |
| [common/result.hpp](../cpp/include/dlt698/common/result.hpp) | 6 | Result 及 value/error 用成功值或 Python 异常、低层回调联合类型适配，不是空实现。 |
| [model/data.hpp](../cpp/include/dlt698/model/data.hpp) | 27 | 模板、Payload、Array/Structure/Null 与 RecordData 的表示采用 Data 工厂和精确 as_* 访问器；静态 type 标签并非需要单独实现的业务。 |
| [model/record.hpp](../cpp/include/dlt698/model/record.hpp) | 6 | variant/vector 类型别名和模板访问器转换为 Python 联合类型、list 及 Data 精确工厂/访问器；具体 Selector/ROAD/MS 值与 codec 已绑定。 |
| [protocol/apdu/advanced.hpp](../cpp/include/dlt698/protocol/apdu/advanced.hpp) | 9 | encode/decode_advanced 没有独立入口，可用 encode/decode_apdu；variant 和 ProxyTarget 模板由 Python 联合类型和具体值类适配。 |
| [protocol/apdu/apdu.hpp](../cpp/include/dlt698/protocol/apdu/apdu.hpp) | 1 | Apdu variant 用具体消息类和 Python 联合类型适配；完整 encode/decode_apdu 已实现。 |
| [protocol/apdu/connection.hpp](../cpp/include/dlt698/protocol/apdu/connection.hpp) | 4 | 分服务 encode/decode_connection 未独立公开，可用 encode/decode_apdu；variant 已适配具体消息。 |
| [protocol/apdu/get.hpp](../cpp/include/dlt698/protocol/apdu/get.hpp) | 5 | 分服务 encode/decode_get 未独立公开，可用 encode/decode_apdu；FollowReport/RecordRow 用具体值、联合类型和 list 适配。 |
| [protocol/apdu/get_block.hpp](../cpp/include/dlt698/protocol/apdu/get_block.hpp) | 1 | GetSnapshot 用 Python 联合类型适配；GetBlockTransfer 已绑定。 |
| [protocol/apdu/mutation.hpp](../cpp/include/dlt698/protocol/apdu/mutation.hpp) | 3 | 分服务 encode/decode_mutation 未独立公开，可用 encode/decode_apdu；variant 已适配具体消息。 |
| [protocol/apdu/security.hpp](../cpp/include/dlt698/protocol/apdu/security.hpp) | 1 | SecurityApdu variant 用具体消息类及联合类型适配；encode/decode_security 已绑定；真实密码后端仍未实现。 |
| [protocol/apdu/time_tag.hpp](../cpp/include/dlt698/protocol/apdu/time_tag.hpp) | 1 | 独立 valid_time_tag 未绑定；Session 内部已调用原生日期和有效期校验。 |
| [protocol/link/frame.hpp](../cpp/include/dlt698/protocol/link/frame.hpp) | 1 | StreamEvent variant 用 Frame/Error 联合类型适配；FrameStreamDecoder 已绑定。 |
| [service/advanced.hpp](../cpp/include/dlt698/service/advanced.hpp) | 4 | AdvancedService 通过 attach_advanced_services 安装到 raw Session；ProxyProvider 已支持 Python 子类桥接、async_request 和一次完成，ProxyRouter 继承该方法；类和回调类型别名通过安装函数及 Python 类型适配。 |
| [service/object.hpp](../cpp/include/dlt698/service/object.hpp) | 1 | ObjectValue 用 Data/int 联合类型适配；ObjectProvider、ObjectRegistry、MemoryObject 已绑定。 |
| [service/service.hpp](../cpp/include/dlt698/service/service.hpp) | 10 | ClientService 未单独导出，其普通读写/记录可用 SessionHandle/Engine/app.Client；ServerService 可由 attach_services 安装。 |
| [service/sync.hpp](../cpp/include/dlt698/service/sync.hpp) | 12 | SyncClientService 无独立类绑定，可用托管 app.Client；显式驱动低层 Session 的同步适配器没有同等独立入口。 |
| [session/session.hpp](../cpp/include/dlt698/session/session.hpp) | 20 | 9 个 set_* 处理器和 in_executor_thread 已绑定，见主报告 G04 修复；close 以 request_close 暴露；回调类型别名以 Python callable/联合类型适配。Engine 事件队列可观察部分通知。 |
| [session/traffic.hpp](../cpp/include/dlt698/session/traffic.hpp) | 9 | 原始 TrafficEvent 借用视图转换为拥有型 Event/Completion.traffic，方向转换为事件 kind；收发观察已实现，不需要重新做一套协议功能。 |
| [standard/oi.hpp](../cpp/include/dlt698/standard/oi.hpp) | 132 | 132 个 OI 符号常量尚未导出到 Python standard.oi；可用数值和目录查询，见主报告 G08。 |
| [transport/channel.hpp](../cpp/include/dlt698/transport/channel.hpp) | 2 | 回调类型别名用成功值/Error 的 Python 完成函数适配；IChannel 已有子类桥接和原生实现。 |
| [transport/tcp.hpp](../cpp/include/dlt698/transport/tcp.hpp) | 3 | IoRuntime.run 未绑定，可用 run_for；ConnectHandler/AcceptHandler 用 Python callable 适配。 |

## app/client.hpp（2 条）

Client 已通过 NativeClient 和 app.Client 包装；diagnostic/traffic 用拥有型 EventQueue/on_event 适配，未直接暴露后台线程 callable。

- `dlt698::app::ClientOptions::diagnostic`（FIELD_DECL）：`std :: function < void ( const Error & ) > diagnostic`。
- `dlt698::app::ClientOptions::traffic`（FIELD_DECL）：`session :: TrafficHandler traffic`。

## app/server.hpp（2 条）

Server 已通过 NativeServer 和 app.Server/AsyncServer 包装；diagnostic/traffic 用拥有型 EventQueue/on_event 适配。

- `dlt698::app::ServerOptions::diagnostic`（FIELD_DECL）：`std :: function < void ( std :: uint64_t , const Error & ) > diagnostic`。
- `dlt698::app::ServerOptions::traffic`（FIELD_DECL）：`std :: function < void ( std :: uint64_t , const session :: TrafficEvent & ) > traffic`。

## codec/data_codec.hpp（6 条）

这些函数依赖 C++ Reader/Writer，未公开独立增量接口；完整 encode_data/decode_data 已实现。

- `dlt698::codec::read_data`（FUNCTION_DECL）：`model :: Data read_data ( Reader & reader , const Limits & limits , std :: size_t depth = 0 )`。
- `dlt698::codec::read_length`（FUNCTION_DECL）：`std :: size_t read_length ( Reader & reader , std :: size_t limit )`。
- `dlt698::codec::read_oad`（FUNCTION_DECL）：`model :: Oad read_oad ( Reader & reader )`。
- `dlt698::codec::write_data`（FUNCTION_DECL）：`void write_data ( Writer & writer , const model :: Data & value , const Limits & limits , std :: size_t depth = 0 )`。
- `dlt698::codec::write_length`（FUNCTION_DECL）：`void write_length ( Writer & writer , std :: size_t length )`。
- `dlt698::codec::write_oad`（FUNCTION_DECL）：`void write_oad ( Writer & writer , const model :: Oad & value )`。

## codec/record_codec.hpp（6 条）

这些函数依赖 C++ Reader/Writer，未公开独立增量接口；记录描述符模型、Data 工厂和 APDU codec 已实现。

- `dlt698::codec::read_ms`（FUNCTION_DECL）：`model :: Ms read_ms ( Reader & reader , const Limits & limits = { } )`。
- `dlt698::codec::read_rcsd`（FUNCTION_DECL）：`model :: Rcsd read_rcsd ( Reader & reader , const Limits & limits = { } )`。
- `dlt698::codec::read_rsd`（FUNCTION_DECL）：`model :: Rsd read_rsd ( Reader & reader , const Limits & limits = { } )`。
- `dlt698::codec::write_ms`（FUNCTION_DECL）：`void write_ms ( Writer & writer , const model :: Ms & value , const Limits & limits = { } )`。
- `dlt698::codec::write_rcsd`（FUNCTION_DECL）：`void write_rcsd ( Writer & writer , const model :: Rcsd & value , const Limits & limits = { } )`。
- `dlt698::codec::write_rsd`（FUNCTION_DECL）：`void write_rsd ( Writer & writer , const model :: Rsd & value , const Limits & limits = { } )`。

## common/bytes.hpp（24 条）

ByteView/Bytes 用 bytes 适配，DecodeFailure 用 Python 异常适配；Reader/Writer 没有独立 Python 增量接口；from_hex/to_hex 未绑定，可用标准库。

- `dlt698::ByteView`（CLASS_DECL）：`class ByteView`。
- `dlt698::ByteView::data`（CXX_METHOD）：`const std :: uint8_t * data ( ) const noexcept`。
- `dlt698::ByteView::empty`（CXX_METHOD）：`bool empty ( ) const noexcept`。
- `dlt698::ByteView::size`（CXX_METHOD）：`std :: size_t size ( ) const noexcept`。
- `dlt698::ByteView::subview`（CXX_METHOD）：`ByteView subview ( std :: size_t offset , std :: size_t size ) const`。
- `dlt698::Bytes`（TYPE_ALIAS_DECL）：`using Bytes = std :: vector < std :: uint8_t >`。
- `dlt698::DecodeFailure`（CLASS_DECL）：`class DLT698_API DecodeFailure final`。
- `dlt698::DecodeFailure::error`（FIELD_DECL）：`Error error`。
- `dlt698::Reader`（CLASS_DECL）：`class Reader`。
- `dlt698::Reader::be`（CXX_METHOD）：`std :: uint64_t be ( std :: size_t n , const char * field )`。
- `dlt698::Reader::bytes`（CXX_METHOD）：`Bytes bytes ( std :: size_t n , const char * field )`。
- `dlt698::Reader::finish`（CXX_METHOD）：`void finish ( ) const`。
- `dlt698::Reader::position`（CXX_METHOD）：`std :: size_t position ( ) const noexcept`。
- `dlt698::Reader::remaining`（CXX_METHOD）：`std :: size_t remaining ( ) const noexcept`。
- `dlt698::Reader::require`（CXX_METHOD）：`void require ( std :: size_t n , const char * field ) const`。
- `dlt698::Reader::u8`（CXX_METHOD）：`std :: uint8_t u8 ( const char * field = "byte" )`。
- `dlt698::Writer`（CLASS_DECL）：`class Writer`。
- `dlt698::Writer::be`（CXX_METHOD）：`void be ( std :: uint64_t value , std :: size_t n )`。
- `dlt698::Writer::bytes`（CXX_METHOD）：`void bytes ( ByteView value )`。
- `dlt698::Writer::size`（CXX_METHOD）：`std :: size_t size ( ) const noexcept`。
- `dlt698::Writer::take`（CXX_METHOD）：`Bytes take ( )`。
- `dlt698::Writer::u8`（CXX_METHOD）：`void u8 ( std :: uint8_t value )`。
- `dlt698::from_hex`（FUNCTION_DECL）：`Result < Bytes > from_hex ( std :: string_view text )`。
- `dlt698::to_hex`（FUNCTION_DECL）：`std :: string to_hex ( ByteView bytes )`。

## common/executor.hpp（2 条）

Clock/Task 类型别名用秒数和 Python callable 适配，执行器本身已绑定。

- `dlt698::IExecutor::Clock`（TYPE_ALIAS_DECL）：`using Clock = std :: chrono :: steady_clock`。
- `dlt698::IExecutor::Task`（TYPE_ALIAS_DECL）：`using Task = std :: function < void ( ) >`。

## common/md5.hpp（1 条）

独立原生 md5 未绑定；服务端 GET MD5 已在 AdvancedService 实现，也可用 Python hashlib。

- `dlt698::md5`（FUNCTION_DECL）：`std :: array < std :: uint8_t , 16 > md5 ( ByteView bytes )`。

## common/result.hpp（6 条）

Result 及 value/error 用成功值或 Python 异常、低层回调联合类型适配，不是空实现。

- `dlt698::Result`（CLASS_DECL）：`template < > class [ [ nodiscard ] ] Result < void >`。
- `dlt698::Result`（CLASS_TEMPLATE）：`template < class T > class [ [ nodiscard ] ] Result`。
- `dlt698::Result::error`（CXX_METHOD）：`const Error & error ( ) const`。
- `dlt698::Result::value`（CXX_METHOD）：`T & value ( ) &`。
- `dlt698::Result::value`（CXX_METHOD）：`T && value ( ) &&`。
- `dlt698::Result::value`（CXX_METHOD）：`const T & value ( ) const &`。

## model/data.hpp（27 条）

模板、Payload、Array/Structure/Null 与 RecordData 的表示采用 Data 工厂和精确 as_* 访问器；静态 type 标签并非需要单独实现的业务。

- `dlt698::model::Array`（STRUCT_DECL）：`struct Array`。
- `dlt698::model::Array::type`（VAR_DECL）：`static constexpr DataType type = DataType :: array`。
- `dlt698::model::Array::value`（FIELD_DECL）：`std :: vector < Data > value`。
- `dlt698::model::BitString::type`（VAR_DECL）：`static constexpr DataType type = DataType :: bit_string`。
- `dlt698::model::Comdcb::type`（VAR_DECL）：`static constexpr DataType type = DataType :: comdcb`。
- `dlt698::model::Data::Data`（FUNCTION_TEMPLATE）：`template < class T , std :: enable_if_t < ! std :: is_same_v < std :: decay_t < T > , Data > , int > = 0 > Data ( T && value )`。
- `dlt698::model::Data::Payload`（TYPE_ALIAS_DECL）：`using Payload = std :: variant < Null , Array , Structure , Boolean , BitString , Int8 , Int16 , Int32 , Int64 , UInt8 , UInt16 , UInt32 , UInt64 , Enum , Float32 , Float64 , OctetString , VisibleString , Utf8String , DateTime , Date , Time , DateTimeS , Oi , Oad , Omd , Ti , Tsa , ScalerUnit , RecordData , Mac , Rn , Sid , SidMac , Comdcb >`。
- `dlt698::model::Data::as`（FUNCTION_TEMPLATE）：`template < class T > const T & as ( ) const`。
- `dlt698::model::Data::payload`（FIELD_DECL）：`Payload payload = Null`。
- `dlt698::model::Null`（STRUCT_DECL）：`struct Null`。
- `dlt698::model::Null::type`（VAR_DECL）：`static constexpr DataType type = DataType :: null`。
- `dlt698::model::Oad::type`（VAR_DECL）：`static constexpr DataType type = DataType :: oad`。
- `dlt698::model::Omd::type`（VAR_DECL）：`static constexpr DataType type = DataType :: omd`。
- `dlt698::model::RecordData`（CLASS_DECL）：`class RecordData`。
- `dlt698::model::RecordData::RecordData`（FUNCTION_TEMPLATE）：`template < class T > explicit RecordData ( T value )`。
- `dlt698::model::RecordData::as`（FUNCTION_TEMPLATE）：`template < class T > const T & as ( ) const`。
- `dlt698::model::RecordData::type`（CXX_METHOD）：`DataType type ( ) const`。
- `dlt698::model::ScalerUnit::type`（VAR_DECL）：`static constexpr DataType type = DataType :: scaler_unit`。
- `dlt698::model::Sid::type`（VAR_DECL）：`static constexpr DataType type = DataType :: sid`。
- `dlt698::model::SidMac::type`（VAR_DECL）：`static constexpr DataType type = DataType :: sid_mac`。
- `dlt698::model::Structure`（STRUCT_DECL）：`struct Structure`。
- `dlt698::model::Structure::type`（VAR_DECL）：`static constexpr DataType type = DataType :: structure`。
- `dlt698::model::Structure::value`（FIELD_DECL）：`std :: vector < Data > value`。
- `dlt698::model::Ti::type`（VAR_DECL）：`static constexpr DataType type = DataType :: ti`。
- `dlt698::model::Value`（CLASS_TEMPLATE）：`template < DataType Tag , class T > struct Value`。
- `dlt698::model::Value::type`（VAR_DECL）：`static constexpr DataType type = Tag`。
- `dlt698::model::Value::value`（FIELD_DECL）：`T value`。

## model/record.hpp（6 条）

variant/vector 类型别名和模板访问器转换为 Python 联合类型、list 及 Data 精确工厂/访问器；具体 Selector/ROAD/MS 值与 codec 已绑定。

- `dlt698::model::Csd`（TYPE_ALIAS_DECL）：`using Csd = std :: variant < Oad , Road >`。
- `dlt698::model::Ms`（TYPE_ALIAS_DECL）：`using Ms = std :: variant < NoMeters , AllMeters , MeterTypes , MeterAddresses , MeterNumbers , MeterTypeRegions , MeterAddressRegions , MeterNumberRegions >`。
- `dlt698::model::Rcsd`（TYPE_ALIAS_DECL）：`using Rcsd = std :: vector < Csd >`。
- `dlt698::model::RecordData`（FUNCTION_TEMPLATE）：`template < class T > RecordData :: RecordData ( T value )`。
- `dlt698::model::Rsd`（TYPE_ALIAS_DECL）：`using Rsd = std :: variant < SelectAll , Selector1 , Selector2 , Selector3 , Selector4 , Selector5 , Selector6 , Selector7 , Selector8 , Selector9 , Selector10 >`。
- `dlt698::model::as`（FUNCTION_TEMPLATE）：`template < class T > const T & RecordData :: as ( ) const`。

## protocol/apdu/advanced.hpp（9 条）

encode/decode_advanced 没有独立入口，可用 encode/decode_apdu；variant 和 ProxyTarget 模板由 Python 联合类型和具体值类适配。

- `dlt698::protocol::apdu::AdvancedApdu`（TYPE_ALIAS_DECL）：`using AdvancedApdu = std :: variant < SetThenGetRequest , SetThenGetResponse , ActionThenGetRequest , ActionThenGetResponse , ReportNotification , ReportResponse , ProxyRequest , ProxyResponse >`。
- `dlt698::protocol::apdu::ProxyRequestPayload`（TYPE_ALIAS_DECL）：`using ProxyRequestPayload = std :: variant < std :: vector < ProxyTarget < model :: Oad >> , ProxyRecordRequest , std :: vector < ProxyTarget < SetAttribute >> , std :: vector < ProxyTarget < SetThenGet >> , std :: vector < ProxyTarget < ActionMethod >> , std :: vector < ProxyTarget < ActionThenGet >> , ProxyTransRequest >`。
- `dlt698::protocol::apdu::ProxyResponsePayload`（TYPE_ALIAS_DECL）：`using ProxyResponsePayload = std :: variant < std :: vector < ProxyTarget < AttributeResult >> , ProxyRecordResponse , std :: vector < ProxyTarget < SetResult >> , std :: vector < ProxyTarget < SetThenGetResult >> , std :: vector < ProxyTarget < ActionResult >> , std :: vector < ProxyTarget < ActionThenGetResult >> , ProxyTransResponse >`。
- `dlt698::protocol::apdu::ProxyTarget`（CLASS_TEMPLATE）：`template < class T > struct ProxyTarget`。
- `dlt698::protocol::apdu::ProxyTarget::items`（FIELD_DECL）：`std :: vector < T > items`。
- `dlt698::protocol::apdu::ProxyTarget::server`（FIELD_DECL）：`model :: Tsa server`。
- `dlt698::protocol::apdu::ProxyTarget::timeout_seconds`（FIELD_DECL）：`std :: uint16_t timeout_seconds = 0`。
- `dlt698::protocol::apdu::decode_advanced`（FUNCTION_DECL）：`Result < AdvancedApdu > decode_advanced ( ByteView bytes , const Limits & limits = { } )`。
- `dlt698::protocol::apdu::encode_advanced`（FUNCTION_DECL）：`Result < Bytes > encode_advanced ( const AdvancedApdu & message , const Limits & limits = { } )`。

## protocol/apdu/apdu.hpp（1 条）

Apdu variant 用具体消息类和 Python 联合类型适配；完整 encode/decode_apdu 已实现。

- `dlt698::protocol::apdu::Apdu`（TYPE_ALIAS_DECL）：`using Apdu = std :: variant < LinkRequest , LinkResponse , ConnectRequest , ConnectResponse , ReleaseRequest , ReleaseResponse , ReleaseNotification , ErrorResponse , GetRequest , GetResponse , SetRequest , SetResponse , ActionRequest , ActionResponse , GetRecordRequest , GetRecordResponse , GetNextRequest , GetNextResponse , GetMd5Request , GetMd5Response , SetThenGetRequest , SetThenGetResponse , ActionThenGetRequest , ActionThenGetResponse , ReportNotification , ReportResponse , ProxyRequest , ProxyResponse >`。

## protocol/apdu/connection.hpp（4 条）

分服务 encode/decode_connection 未独立公开，可用 encode/decode_apdu；variant 已适配具体消息。

- `dlt698::protocol::apdu::ConnectMechanism`（TYPE_ALIAS_DECL）：`using ConnectMechanism = std :: variant < NullSecurity , PasswordSecurity , SymmetrySecurity , SignatureSecurity >`。
- `dlt698::protocol::apdu::ConnectionApdu`（TYPE_ALIAS_DECL）：`using ConnectionApdu = std :: variant < LinkRequest , LinkResponse , ConnectRequest , ConnectResponse , ReleaseRequest , ReleaseResponse , ReleaseNotification , ErrorResponse >`。
- `dlt698::protocol::apdu::decode_connection`（FUNCTION_DECL）：`Result < ConnectionApdu > decode_connection ( ByteView bytes , const Limits & limits = { } )`。
- `dlt698::protocol::apdu::encode_connection`（FUNCTION_DECL）：`Result < Bytes > encode_connection ( const ConnectionApdu & message , const Limits & limits = { } )`。

## protocol/apdu/get.hpp（5 条）

分服务 encode/decode_get 未独立公开，可用 encode/decode_apdu；FollowReport/RecordRow 用具体值、联合类型和 list 适配。

- `dlt698::protocol::apdu::FollowReport`（TYPE_ALIAS_DECL）：`using FollowReport = std :: variant < std :: vector < AttributeResult > , std :: vector < RecordResult >>`。
- `dlt698::protocol::apdu::GetApdu`（TYPE_ALIAS_DECL）：`using GetApdu = std :: variant < GetRequest , GetResponse , GetRecordRequest , GetRecordResponse , GetNextRequest , GetNextResponse , GetMd5Request , GetMd5Response >`。
- `dlt698::protocol::apdu::RecordRow`（TYPE_ALIAS_DECL）：`using RecordRow = std :: vector < model :: Data >`。
- `dlt698::protocol::apdu::decode_get`（FUNCTION_DECL）：`Result < GetApdu > decode_get ( ByteView bytes , const Limits & limits = { } )`。
- `dlt698::protocol::apdu::encode_get`（FUNCTION_DECL）：`Result < Bytes > encode_get ( const GetApdu & apdu , const Limits & limits = { } )`。

## protocol/apdu/get_block.hpp（1 条）

GetSnapshot 用 Python 联合类型适配；GetBlockTransfer 已绑定。

- `dlt698::protocol::apdu::GetSnapshot`（TYPE_ALIAS_DECL）：`using GetSnapshot = std :: variant < GetResponse , GetRecordResponse >`。

## protocol/apdu/mutation.hpp（3 条）

分服务 encode/decode_mutation 未独立公开，可用 encode/decode_apdu；variant 已适配具体消息。

- `dlt698::protocol::apdu::MutationApdu`（TYPE_ALIAS_DECL）：`using MutationApdu = std :: variant < SetRequest , SetResponse , ActionRequest , ActionResponse >`。
- `dlt698::protocol::apdu::decode_mutation`（FUNCTION_DECL）：`Result < MutationApdu > decode_mutation ( ByteView bytes , const Limits & limits = { } )`。
- `dlt698::protocol::apdu::encode_mutation`（FUNCTION_DECL）：`Result < Bytes > encode_mutation ( const MutationApdu & message , const Limits & limits = { } )`。

## protocol/apdu/security.hpp（1 条）

SecurityApdu variant 用具体消息类及联合类型适配；encode/decode_security 已绑定；真实密码后端仍未实现。

- `dlt698::protocol::apdu::SecurityApdu`（TYPE_ALIAS_DECL）：`using SecurityApdu = std :: variant < SecurityRequest , SecurityResponse >`。

## protocol/apdu/time_tag.hpp（1 条）

独立 valid_time_tag 未绑定；Session 内部已调用原生日期和有效期校验。

- `dlt698::protocol::apdu::valid_time_tag`（FUNCTION_DECL）：`Result < bool > valid_time_tag ( const TimeTag & tag , const model :: DateTimeS & now )`。

## protocol/link/frame.hpp（1 条）

StreamEvent variant 用 Frame/Error 联合类型适配；FrameStreamDecoder 已绑定。

- `dlt698::protocol::link::StreamEvent`（TYPE_ALIAS_DECL）：`using StreamEvent = std :: variant < Frame , Error >`。

## service/advanced.hpp（4 条）

AdvancedService 通过 attach_advanced_services 安装到 raw Session；ProxyProvider 已支持 Python 子类桥接、async_request 和一次完成，ProxyRouter 继承该方法；类和回调类型别名通过安装函数及 Python 类型适配。

- `dlt698::service::AdvancedService`（CLASS_DECL）：`class AdvancedService`。
- `dlt698::service::AdvancedServiceOptions::TransHandler`（TYPE_ALIAS_DECL）：`using TransHandler = std :: function < IProxyProvider :: Cancel ( protocol :: apdu :: ProxyTransRequest , std :: function < void ( Result < protocol :: apdu :: ProxyTransResponse > ) > ) >`。
- `dlt698::service::IProxyProvider::Cancel`（TYPE_ALIAS_DECL）：`using Cancel = std :: function < void ( ) >`。
- `dlt698::service::IProxyProvider::Handler`（TYPE_ALIAS_DECL）：`using Handler = session :: Session :: ExchangeHandler`。

## service/object.hpp（1 条）

ObjectValue 用 Data/int 联合类型适配；ObjectProvider、ObjectRegistry、MemoryObject 已绑定。

- `dlt698::service::ObjectValue`（TYPE_ALIAS_DECL）：`using ObjectValue = std :: variant < std :: uint8_t , model :: Data >`。

## service/service.hpp（10 条）

ClientService 未单独导出，其普通读写/记录可用 SessionHandle/Engine/app.Client；ServerService 可由 attach_services 安装。

- `dlt698::service::ClientService`（CLASS_DECL）：`class ClientService`。
- `dlt698::service::ClientService::async_action`（CXX_METHOD）：`void async_action ( model :: Omd method , model :: Data parameter , std :: function < void ( Result < ActionValue > ) > handler )`。
- `dlt698::service::ClientService::async_action_list`（CXX_METHOD）：`void async_action_list ( std :: vector < protocol :: apdu :: ActionMethod > methods , session :: Session :: ActionHandler handler )`。
- `dlt698::service::ClientService::async_get`（CXX_METHOD）：`void async_get ( model :: Oad attribute , std :: function < void ( Result < ObjectValue > ) > handler )`。
- `dlt698::service::ClientService::async_get_list`（CXX_METHOD）：`void async_get_list ( std :: vector < model :: Oad > attributes , session :: Session :: GetHandler handler )`。
- `dlt698::service::ClientService::async_get_record`（CXX_METHOD）：`void async_get_record ( protocol :: apdu :: GetRecord record , std :: function < void ( Result < protocol :: apdu :: RecordResult > ) > handler )`。
- `dlt698::service::ClientService::async_get_record_list`（CXX_METHOD）：`void async_get_record_list ( std :: vector < protocol :: apdu :: GetRecord > records , session :: Session :: RecordHandler handler )`。
- `dlt698::service::ClientService::async_set`（CXX_METHOD）：`void async_set ( model :: Oad attribute , model :: Data value , std :: function < void ( Result < std :: uint8_t > ) > handler )`。
- `dlt698::service::ClientService::async_set_list`（CXX_METHOD）：`void async_set_list ( std :: vector < protocol :: apdu :: SetAttribute > attributes , session :: Session :: SetHandler handler )`。
- `dlt698::service::ServerService`（CLASS_DECL）：`class ServerService`。

## service/sync.hpp（12 条）

SyncClientService 无独立类绑定，可用托管 app.Client；显式驱动低层 Session 的同步适配器没有同等独立入口。

- `dlt698::service::SyncClientService`（CLASS_DECL）：`class SyncClientService`。
- `dlt698::service::SyncClientService::Drive`（TYPE_ALIAS_DECL）：`using Drive = std :: function < void ( std :: chrono :: milliseconds ) >`。
- `dlt698::service::SyncClientService::action`（CXX_METHOD）：`Result < ActionValue > action ( model :: Omd method , model :: Data parameter )`。
- `dlt698::service::SyncClientService::action_list`（CXX_METHOD）：`Result < protocol :: apdu :: ActionResponse > action_list ( std :: vector < protocol :: apdu :: ActionMethod > methods )`。
- `dlt698::service::SyncClientService::connect`（CXX_METHOD）：`Result < protocol :: apdu :: ConnectResponse > connect ( )`。
- `dlt698::service::SyncClientService::get`（CXX_METHOD）：`Result < ObjectValue > get ( model :: Oad attribute )`。
- `dlt698::service::SyncClientService::get_list`（CXX_METHOD）：`Result < protocol :: apdu :: GetResponse > get_list ( std :: vector < model :: Oad > attributes )`。
- `dlt698::service::SyncClientService::get_record`（CXX_METHOD）：`Result < protocol :: apdu :: RecordResult > get_record ( protocol :: apdu :: GetRecord record )`。
- `dlt698::service::SyncClientService::get_record_list`（CXX_METHOD）：`Result < protocol :: apdu :: GetRecordResponse > get_record_list ( std :: vector < protocol :: apdu :: GetRecord > records )`。
- `dlt698::service::SyncClientService::release`（CXX_METHOD）：`Result < void > release ( )`。
- `dlt698::service::SyncClientService::set`（CXX_METHOD）：`Result < std :: uint8_t > set ( model :: Oad attribute , model :: Data value )`。
- `dlt698::service::SyncClientService::set_list`（CXX_METHOD）：`Result < protocol :: apdu :: SetResponse > set_list ( std :: vector < protocol :: apdu :: SetAttribute > attributes )`。

## session/session.hpp（20 条）

9 个 set_* 处理器和 in_executor_thread 已绑定，见主报告 G04 修复；close 以 request_close 暴露；回调类型别名以 Python callable/联合类型适配。Engine 事件队列可观察部分通知。

- `dlt698::session::Session::ActionHandler`（TYPE_ALIAS_DECL）：`using ActionHandler = std :: function < void ( Result < protocol :: apdu :: ActionResponse > ) >`。
- `dlt698::session::Session::ActionRequestHandler`（TYPE_ALIAS_DECL）：`using ActionRequestHandler = std :: function < protocol :: apdu :: ActionResponse ( const protocol :: apdu :: ActionRequest & ) >`。
- `dlt698::session::Session::AdvancedRequestHandler`（TYPE_ALIAS_DECL）：`using AdvancedRequestHandler = std :: function < BackendCancel ( protocol :: apdu :: Apdu , ExchangeHandler ) >`。
- `dlt698::session::Session::BackendCancel`（TYPE_ALIAS_DECL）：`using BackendCancel = std :: function < void ( ) >`。
- `dlt698::session::Session::CloseHandler`（TYPE_ALIAS_DECL）：`using CloseHandler = std :: function < void ( const Error & ) >`。
- `dlt698::session::Session::ConnectHandler`（TYPE_ALIAS_DECL）：`using ConnectHandler = std :: function < void ( Result < protocol :: apdu :: ConnectResponse > ) >`。
- `dlt698::session::Session::DiagnosticHandler`（TYPE_ALIAS_DECL）：`using DiagnosticHandler = std :: function < void ( const Error & ) >`。
- `dlt698::session::Session::ExchangeHandler`（TYPE_ALIAS_DECL）：`using ExchangeHandler = std :: function < void ( Result < protocol :: apdu :: Apdu > ) >`。
- `dlt698::session::Session::FollowHandler`（TYPE_ALIAS_DECL）：`using FollowHandler = std :: function < void ( const protocol :: apdu :: FollowReport & ) >`。
- `dlt698::session::Session::GetHandler`（TYPE_ALIAS_DECL）：`using GetHandler = std :: function < void ( Result < protocol :: apdu :: GetResponse > ) >`。
- `dlt698::session::Session::LinkHandler`（TYPE_ALIAS_DECL）：`using LinkHandler = std :: function < void ( Result < protocol :: apdu :: LinkResponse > ) >`。
- `dlt698::session::Session::RecordHandler`（TYPE_ALIAS_DECL）：`using RecordHandler = std :: function < void ( Result < protocol :: apdu :: GetRecordResponse > ) >`。
- `dlt698::session::Session::RecordRequestHandler`（TYPE_ALIAS_DECL）：`using RecordRequestHandler = std :: function < protocol :: apdu :: GetRecordResponse ( const protocol :: apdu :: GetRecordRequest & ) >`。
- `dlt698::session::Session::ReleaseHandler`（TYPE_ALIAS_DECL）：`using ReleaseHandler = std :: function < void ( Result < void > ) >`。
- `dlt698::session::Session::ReportHandler`（TYPE_ALIAS_DECL）：`using ReportHandler = std :: function < bool ( const protocol :: apdu :: ReportNotification & ) >`。
- `dlt698::session::Session::RequestHandler`（TYPE_ALIAS_DECL）：`using RequestHandler = std :: function < protocol :: apdu :: GetResponse ( const protocol :: apdu :: GetRequest & ) >`。
- `dlt698::session::Session::SetHandler`（TYPE_ALIAS_DECL）：`using SetHandler = std :: function < void ( Result < protocol :: apdu :: SetResponse > ) >`。
- `dlt698::session::Session::SetRequestHandler`（TYPE_ALIAS_DECL）：`using SetRequestHandler = std :: function < protocol :: apdu :: SetResponse ( const protocol :: apdu :: SetRequest & ) >`。
- `dlt698::session::Session::StateHandler`（TYPE_ALIAS_DECL）：`using StateHandler = std :: function < void ( State ) >`。
- `dlt698::session::Session::close`（CXX_METHOD）：`void close ( )`。

## session/traffic.hpp（9 条）

原始 TrafficEvent 借用视图转换为拥有型 Event/Completion.traffic，方向转换为事件 kind；收发观察已实现，不需要重新做一套协议功能。

- `dlt698::session::TrafficDirection`（ENUM_DECL）：`enum class TrafficDirection`。
- `dlt698::session::TrafficDirection::receive`（ENUM_CONSTANT_DECL）：`receive`。
- `dlt698::session::TrafficDirection::send`（ENUM_CONSTANT_DECL）：`send`。
- `dlt698::session::TrafficEvent`（STRUCT_DECL）：`struct TrafficEvent`。
- `dlt698::session::TrafficEvent::bytes`（FIELD_DECL）：`ByteView bytes`。
- `dlt698::session::TrafficEvent::direction`（FIELD_DECL）：`TrafficDirection direction`。
- `dlt698::session::TrafficEvent::result`（FIELD_DECL）：`Result < void > result`。
- `dlt698::session::TrafficEvent::timestamp`（FIELD_DECL）：`std :: chrono :: system_clock :: time_point timestamp`。
- `dlt698::session::TrafficHandler`（TYPE_ALIAS_DECL）：`using TrafficHandler = std :: function < void ( const TrafficEvent & ) >`。

## standard/oi.hpp（132 条）

132 个 OI 符号常量尚未导出到 Python standard.oi；可用数值和目录查询，见主报告 G08。

- `dlt698::standard::oi::active_accuracy_class`（VAR_DECL）：`inline constexpr std :: uint16_t active_accuracy_class = 0x4107`。
- `dlt698::standard::oi::active_power`（VAR_DECL）：`inline constexpr std :: uint16_t active_power = 0x2004`。
- `dlt698::standard::oi::apparent_power`（VAR_DECL）：`inline constexpr std :: uint16_t apparent_power = 0x2006`。
- `dlt698::standard::oi::asset_code`（VAR_DECL）：`inline constexpr std :: uint16_t asset_code = 0x4103`。
- `dlt698::standard::oi::clock_source`（VAR_DECL）：`inline constexpr std :: uint16_t clock_source = 0x4006`。
- `dlt698::standard::oi::combination_active_energy`（VAR_DECL）：`inline constexpr std :: uint16_t combination_active_energy = 0x0000`。
- `dlt698::standard::oi::combination_reactive_1_maximum_demand`（VAR_DECL）：`inline constexpr std :: uint16_t combination_reactive_1_maximum_demand = 0x1030`。
- `dlt698::standard::oi::combination_reactive_1_maximum_demand_a`（VAR_DECL）：`inline constexpr std :: uint16_t combination_reactive_1_maximum_demand_a = 0x1031`。
- `dlt698::standard::oi::combination_reactive_1_maximum_demand_b`（VAR_DECL）：`inline constexpr std :: uint16_t combination_reactive_1_maximum_demand_b = 0x1032`。
- `dlt698::standard::oi::combination_reactive_1_maximum_demand_c`（VAR_DECL）：`inline constexpr std :: uint16_t combination_reactive_1_maximum_demand_c = 0x1033`。
- `dlt698::standard::oi::combination_reactive_2_maximum_demand`（VAR_DECL）：`inline constexpr std :: uint16_t combination_reactive_2_maximum_demand = 0x1040`。
- `dlt698::standard::oi::combination_reactive_2_maximum_demand_a`（VAR_DECL）：`inline constexpr std :: uint16_t combination_reactive_2_maximum_demand_a = 0x1041`。
- `dlt698::standard::oi::combination_reactive_2_maximum_demand_b`（VAR_DECL）：`inline constexpr std :: uint16_t combination_reactive_2_maximum_demand_b = 0x1042`。
- `dlt698::standard::oi::combination_reactive_2_maximum_demand_c`（VAR_DECL）：`inline constexpr std :: uint16_t combination_reactive_2_maximum_demand_c = 0x1043`。
- `dlt698::standard::oi::combination_reactive_energy_1`（VAR_DECL）：`inline constexpr std :: uint16_t combination_reactive_energy_1 = 0x0030`。
- `dlt698::standard::oi::combination_reactive_energy_1_a`（VAR_DECL）：`inline constexpr std :: uint16_t combination_reactive_energy_1_a = 0x0031`。
- `dlt698::standard::oi::combination_reactive_energy_1_b`（VAR_DECL）：`inline constexpr std :: uint16_t combination_reactive_energy_1_b = 0x0032`。
- `dlt698::standard::oi::combination_reactive_energy_1_c`（VAR_DECL）：`inline constexpr std :: uint16_t combination_reactive_energy_1_c = 0x0033`。
- `dlt698::standard::oi::combination_reactive_energy_2`（VAR_DECL）：`inline constexpr std :: uint16_t combination_reactive_energy_2 = 0x0040`。
- `dlt698::standard::oi::combination_reactive_energy_2_a`（VAR_DECL）：`inline constexpr std :: uint16_t combination_reactive_energy_2_a = 0x0041`。
- `dlt698::standard::oi::combination_reactive_energy_2_b`（VAR_DECL）：`inline constexpr std :: uint16_t combination_reactive_energy_2_b = 0x0042`。
- `dlt698::standard::oi::combination_reactive_energy_2_c`（VAR_DECL）：`inline constexpr std :: uint16_t combination_reactive_energy_2_c = 0x0043`。
- `dlt698::standard::oi::communication_address`（VAR_DECL）：`inline constexpr std :: uint16_t communication_address = 0x4001`。
- `dlt698::standard::oi::current`（VAR_DECL）：`inline constexpr std :: uint16_t current = 0x2001`。
- `dlt698::standard::oi::current_active_demand`（VAR_DECL）：`inline constexpr std :: uint16_t current_active_demand = 0x2017`。
- `dlt698::standard::oi::current_apparent_demand`（VAR_DECL）：`inline constexpr std :: uint16_t current_apparent_demand = 0x2019`。
- `dlt698::standard::oi::current_distortion`（VAR_DECL）：`inline constexpr std :: uint16_t current_distortion = 0x200c`。
- `dlt698::standard::oi::current_harmonics`（VAR_DECL）：`inline constexpr std :: uint16_t current_harmonics = 0x200e`。
- `dlt698::standard::oi::current_reactive_demand`（VAR_DECL）：`inline constexpr std :: uint16_t current_reactive_demand = 0x2018`。
- `dlt698::standard::oi::customer_number`（VAR_DECL）：`inline constexpr std :: uint16_t customer_number = 0x4003`。
- `dlt698::standard::oi::daily_freeze`（VAR_DECL）：`inline constexpr std :: uint16_t daily_freeze = 0x5004`。
- `dlt698::standard::oi::date_time`（VAR_DECL）：`inline constexpr std :: uint16_t date_time = 0x4000`。
- `dlt698::standard::oi::esam`（VAR_DECL）：`inline constexpr std :: uint16_t esam = 0xf100`。
- `dlt698::standard::oi::event_end_time`（VAR_DECL）：`inline constexpr std :: uint16_t event_end_time = 0x2020`。
- `dlt698::standard::oi::event_sequence`（VAR_DECL）：`inline constexpr std :: uint16_t event_sequence = 0x2022`。
- `dlt698::standard::oi::event_source`（VAR_DECL）：`inline constexpr std :: uint16_t event_source = 0x2024`。
- `dlt698::standard::oi::event_start_time`（VAR_DECL）：`inline constexpr std :: uint16_t event_start_time = 0x201e`。
- `dlt698::standard::oi::follow_report_status`（VAR_DECL）：`inline constexpr std :: uint16_t follow_report_status = 0x2015`。
- `dlt698::standard::oi::forward_active_energy`（VAR_DECL）：`inline constexpr std :: uint16_t forward_active_energy = 0x0010`。
- `dlt698::standard::oi::forward_active_energy_a`（VAR_DECL）：`inline constexpr std :: uint16_t forward_active_energy_a = 0x0011`。
- `dlt698::standard::oi::forward_active_energy_b`（VAR_DECL）：`inline constexpr std :: uint16_t forward_active_energy_b = 0x0012`。
- `dlt698::standard::oi::forward_active_energy_c`（VAR_DECL）：`inline constexpr std :: uint16_t forward_active_energy_c = 0x0013`。
- `dlt698::standard::oi::forward_active_maximum_demand`（VAR_DECL）：`inline constexpr std :: uint16_t forward_active_maximum_demand = 0x1010`。
- `dlt698::standard::oi::forward_active_maximum_demand_a`（VAR_DECL）：`inline constexpr std :: uint16_t forward_active_maximum_demand_a = 0x1011`。
- `dlt698::standard::oi::forward_active_maximum_demand_b`（VAR_DECL）：`inline constexpr std :: uint16_t forward_active_maximum_demand_b = 0x1012`。
- `dlt698::standard::oi::forward_active_maximum_demand_c`（VAR_DECL）：`inline constexpr std :: uint16_t forward_active_maximum_demand_c = 0x1013`。
- `dlt698::standard::oi::forward_apparent_energy`（VAR_DECL）：`inline constexpr std :: uint16_t forward_apparent_energy = 0x0090`。
- `dlt698::standard::oi::forward_apparent_energy_a`（VAR_DECL）：`inline constexpr std :: uint16_t forward_apparent_energy_a = 0x0091`。
- `dlt698::standard::oi::forward_apparent_energy_b`（VAR_DECL）：`inline constexpr std :: uint16_t forward_apparent_energy_b = 0x0092`。
- `dlt698::standard::oi::forward_apparent_energy_c`（VAR_DECL）：`inline constexpr std :: uint16_t forward_apparent_energy_c = 0x0093`。
- `dlt698::standard::oi::forward_apparent_maximum_demand`（VAR_DECL）：`inline constexpr std :: uint16_t forward_apparent_maximum_demand = 0x1090`。
- `dlt698::standard::oi::forward_apparent_maximum_demand_a`（VAR_DECL）：`inline constexpr std :: uint16_t forward_apparent_maximum_demand_a = 0x1091`。
- `dlt698::standard::oi::forward_apparent_maximum_demand_b`（VAR_DECL）：`inline constexpr std :: uint16_t forward_apparent_maximum_demand_b = 0x1092`。
- `dlt698::standard::oi::forward_apparent_maximum_demand_c`（VAR_DECL）：`inline constexpr std :: uint16_t forward_apparent_maximum_demand_c = 0x1093`。
- `dlt698::standard::oi::freeze_sequence`（VAR_DECL）：`inline constexpr std :: uint16_t freeze_sequence = 0x2023`。
- `dlt698::standard::oi::freeze_time`（VAR_DECL）：`inline constexpr std :: uint16_t freeze_time = 0x2021`。
- `dlt698::standard::oi::frequency`（VAR_DECL）：`inline constexpr std :: uint16_t frequency = 0x200f`。
- `dlt698::standard::oi::harmonic_analysis_order`（VAR_DECL）：`inline constexpr std :: uint16_t harmonic_analysis_order = 0x400e`。
- `dlt698::standard::oi::internal_temperature`（VAR_DECL）：`inline constexpr std :: uint16_t internal_temperature = 0x2010`。
- `dlt698::standard::oi::maximum_current`（VAR_DECL）：`inline constexpr std :: uint16_t maximum_current = 0x4106`。
- `dlt698::standard::oi::maximum_demand_period`（VAR_DECL）：`inline constexpr std :: uint16_t maximum_demand_period = 0x4100`。
- `dlt698::standard::oi::meter_model`（VAR_DECL）：`inline constexpr std :: uint16_t meter_model = 0x410b`。
- `dlt698::standard::oi::meter_number`（VAR_DECL）：`inline constexpr std :: uint16_t meter_number = 0x4002`。
- `dlt698::standard::oi::meter_power_down_event`（VAR_DECL）：`inline constexpr std :: uint16_t meter_power_down_event = 0x3011`。
- `dlt698::standard::oi::metering_element_count`（VAR_DECL）：`inline constexpr std :: uint16_t metering_element_count = 0x4010`。
- `dlt698::standard::oi::monthly_freeze`（VAR_DECL）：`inline constexpr std :: uint16_t monthly_freeze = 0x5006`。
- `dlt698::standard::oi::operating_status`（VAR_DECL）：`inline constexpr std :: uint16_t operating_status = 0x2014`。
- `dlt698::standard::oi::power_factor`（VAR_DECL）：`inline constexpr std :: uint16_t power_factor = 0x200a`。
- `dlt698::standard::oi::quadrant_1_reactive_energy`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_1_reactive_energy = 0x0050`。
- `dlt698::standard::oi::quadrant_1_reactive_energy_a`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_1_reactive_energy_a = 0x0051`。
- `dlt698::standard::oi::quadrant_1_reactive_energy_b`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_1_reactive_energy_b = 0x0052`。
- `dlt698::standard::oi::quadrant_1_reactive_energy_c`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_1_reactive_energy_c = 0x0053`。
- `dlt698::standard::oi::quadrant_1_reactive_maximum_demand`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_1_reactive_maximum_demand = 0x1050`。
- `dlt698::standard::oi::quadrant_1_reactive_maximum_demand_a`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_1_reactive_maximum_demand_a = 0x1051`。
- `dlt698::standard::oi::quadrant_1_reactive_maximum_demand_b`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_1_reactive_maximum_demand_b = 0x1052`。
- `dlt698::standard::oi::quadrant_1_reactive_maximum_demand_c`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_1_reactive_maximum_demand_c = 0x1053`。
- `dlt698::standard::oi::quadrant_2_reactive_energy`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_2_reactive_energy = 0x0060`。
- `dlt698::standard::oi::quadrant_2_reactive_energy_a`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_2_reactive_energy_a = 0x0061`。
- `dlt698::standard::oi::quadrant_2_reactive_energy_b`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_2_reactive_energy_b = 0x0062`。
- `dlt698::standard::oi::quadrant_2_reactive_energy_c`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_2_reactive_energy_c = 0x0063`。
- `dlt698::standard::oi::quadrant_2_reactive_maximum_demand`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_2_reactive_maximum_demand = 0x1060`。
- `dlt698::standard::oi::quadrant_2_reactive_maximum_demand_a`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_2_reactive_maximum_demand_a = 0x1061`。
- `dlt698::standard::oi::quadrant_2_reactive_maximum_demand_b`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_2_reactive_maximum_demand_b = 0x1062`。
- `dlt698::standard::oi::quadrant_2_reactive_maximum_demand_c`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_2_reactive_maximum_demand_c = 0x1063`。
- `dlt698::standard::oi::quadrant_3_reactive_energy`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_3_reactive_energy = 0x0070`。
- `dlt698::standard::oi::quadrant_3_reactive_energy_a`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_3_reactive_energy_a = 0x0071`。
- `dlt698::standard::oi::quadrant_3_reactive_energy_b`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_3_reactive_energy_b = 0x0072`。
- `dlt698::standard::oi::quadrant_3_reactive_energy_c`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_3_reactive_energy_c = 0x0073`。
- `dlt698::standard::oi::quadrant_3_reactive_maximum_demand`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_3_reactive_maximum_demand = 0x1070`。
- `dlt698::standard::oi::quadrant_3_reactive_maximum_demand_a`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_3_reactive_maximum_demand_a = 0x1071`。
- `dlt698::standard::oi::quadrant_3_reactive_maximum_demand_b`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_3_reactive_maximum_demand_b = 0x1072`。
- `dlt698::standard::oi::quadrant_3_reactive_maximum_demand_c`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_3_reactive_maximum_demand_c = 0x1073`。
- `dlt698::standard::oi::quadrant_4_reactive_energy`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_4_reactive_energy = 0x0080`。
- `dlt698::standard::oi::quadrant_4_reactive_energy_a`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_4_reactive_energy_a = 0x0081`。
- `dlt698::standard::oi::quadrant_4_reactive_energy_b`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_4_reactive_energy_b = 0x0082`。
- `dlt698::standard::oi::quadrant_4_reactive_energy_c`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_4_reactive_energy_c = 0x0083`。
- `dlt698::standard::oi::quadrant_4_reactive_maximum_demand`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_4_reactive_maximum_demand = 0x1080`。
- `dlt698::standard::oi::quadrant_4_reactive_maximum_demand_a`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_4_reactive_maximum_demand_a = 0x1081`。
- `dlt698::standard::oi::quadrant_4_reactive_maximum_demand_b`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_4_reactive_maximum_demand_b = 0x1082`。
- `dlt698::standard::oi::quadrant_4_reactive_maximum_demand_c`（VAR_DECL）：`inline constexpr std :: uint16_t quadrant_4_reactive_maximum_demand_c = 0x1083`。
- `dlt698::standard::oi::rated_current`（VAR_DECL）：`inline constexpr std :: uint16_t rated_current = 0x4105`。
- `dlt698::standard::oi::rated_voltage`（VAR_DECL）：`inline constexpr std :: uint16_t rated_voltage = 0x4104`。
- `dlt698::standard::oi::reactive_accuracy_class`（VAR_DECL）：`inline constexpr std :: uint16_t reactive_accuracy_class = 0x4108`。
- `dlt698::standard::oi::reactive_power`（VAR_DECL）：`inline constexpr std :: uint16_t reactive_power = 0x2005`。
- `dlt698::standard::oi::reverse_active_energy`（VAR_DECL）：`inline constexpr std :: uint16_t reverse_active_energy = 0x0020`。
- `dlt698::standard::oi::reverse_active_energy_a`（VAR_DECL）：`inline constexpr std :: uint16_t reverse_active_energy_a = 0x0021`。
- `dlt698::standard::oi::reverse_active_energy_b`（VAR_DECL）：`inline constexpr std :: uint16_t reverse_active_energy_b = 0x0022`。
- `dlt698::standard::oi::reverse_active_energy_c`（VAR_DECL）：`inline constexpr std :: uint16_t reverse_active_energy_c = 0x0023`。
- `dlt698::standard::oi::reverse_active_maximum_demand`（VAR_DECL）：`inline constexpr std :: uint16_t reverse_active_maximum_demand = 0x1020`。
- `dlt698::standard::oi::reverse_active_maximum_demand_a`（VAR_DECL）：`inline constexpr std :: uint16_t reverse_active_maximum_demand_a = 0x1021`。
- `dlt698::standard::oi::reverse_active_maximum_demand_b`（VAR_DECL）：`inline constexpr std :: uint16_t reverse_active_maximum_demand_b = 0x1022`。
- `dlt698::standard::oi::reverse_active_maximum_demand_c`（VAR_DECL）：`inline constexpr std :: uint16_t reverse_active_maximum_demand_c = 0x1023`。
- `dlt698::standard::oi::reverse_apparent_energy`（VAR_DECL）：`inline constexpr std :: uint16_t reverse_apparent_energy = 0x00a0`。
- `dlt698::standard::oi::reverse_apparent_energy_a`（VAR_DECL）：`inline constexpr std :: uint16_t reverse_apparent_energy_a = 0x00a1`。
- `dlt698::standard::oi::reverse_apparent_energy_b`（VAR_DECL）：`inline constexpr std :: uint16_t reverse_apparent_energy_b = 0x00a2`。
- `dlt698::standard::oi::reverse_apparent_energy_c`（VAR_DECL）：`inline constexpr std :: uint16_t reverse_apparent_energy_c = 0x00a3`。
- `dlt698::standard::oi::reverse_apparent_maximum_demand`（VAR_DECL）：`inline constexpr std :: uint16_t reverse_apparent_maximum_demand = 0x10a0`。
- `dlt698::standard::oi::reverse_apparent_maximum_demand_a`（VAR_DECL）：`inline constexpr std :: uint16_t reverse_apparent_maximum_demand_a = 0x10a1`。
- `dlt698::standard::oi::reverse_apparent_maximum_demand_b`（VAR_DECL）：`inline constexpr std :: uint16_t reverse_apparent_maximum_demand_b = 0x10a2`。
- `dlt698::standard::oi::reverse_apparent_maximum_demand_c`（VAR_DECL）：`inline constexpr std :: uint16_t reverse_apparent_maximum_demand_c = 0x10a3`。
- `dlt698::standard::oi::rs232`（VAR_DECL）：`inline constexpr std :: uint16_t rs232 = 0xf200`。
- `dlt698::standard::oi::rs485`（VAR_DECL）：`inline constexpr std :: uint16_t rs485 = 0xf201`。
- `dlt698::standard::oi::security_mode`（VAR_DECL）：`inline constexpr std :: uint16_t security_mode = 0xf101`。
- `dlt698::standard::oi::sliding_interval`（VAR_DECL）：`inline constexpr std :: uint16_t sliding_interval = 0x4101`。
- `dlt698::standard::oi::step_count`（VAR_DECL）：`inline constexpr std :: uint16_t step_count = 0x400d`。
- `dlt698::standard::oi::terminal_initialization_event`（VAR_DECL）：`inline constexpr std :: uint16_t terminal_initialization_event = 0x3100`。
- `dlt698::standard::oi::time_period_counts`（VAR_DECL）：`inline constexpr std :: uint16_t time_period_counts = 0x400c`。
- `dlt698::standard::oi::voltage`（VAR_DECL）：`inline constexpr std :: uint16_t voltage = 0x2000`。
- `dlt698::standard::oi::voltage_distortion`（VAR_DECL）：`inline constexpr std :: uint16_t voltage_distortion = 0x200b`。
- `dlt698::standard::oi::voltage_harmonics`（VAR_DECL）：`inline constexpr std :: uint16_t voltage_harmonics = 0x200d`。
- `dlt698::standard::oi::voltage_quality_limits`（VAR_DECL）：`inline constexpr std :: uint16_t voltage_quality_limits = 0x4030`。
- `dlt698::standard::oi::weekend_characteristic`（VAR_DECL）：`inline constexpr std :: uint16_t weekend_characteristic = 0x4012`。

## transport/channel.hpp（2 条）

回调类型别名用成功值/Error 的 Python 完成函数适配；IChannel 已有子类桥接和原生实现。

- `dlt698::transport::IChannel::ReadHandler`（TYPE_ALIAS_DECL）：`using ReadHandler = std :: function < void ( Result < Bytes > ) >`。
- `dlt698::transport::IChannel::WriteHandler`（TYPE_ALIAS_DECL）：`using WriteHandler = std :: function < void ( Result < void > ) >`。

## transport/tcp.hpp（3 条）

IoRuntime.run 未绑定，可用 run_for；ConnectHandler/AcceptHandler 用 Python callable 适配。

- `dlt698::transport::IoRuntime::run`（CXX_METHOD）：`void run ( )`。
- `dlt698::transport::TcpChannel::ConnectHandler`（TYPE_ALIAS_DECL）：`using ConnectHandler = std :: function < void ( Result < void > ) >`。
- `dlt698::transport::TcpListener::AcceptHandler`（TYPE_ALIAS_DECL）：`using AcceptHandler = std :: function < void ( Result < std :: shared_ptr < TcpChannel >> ) >`。

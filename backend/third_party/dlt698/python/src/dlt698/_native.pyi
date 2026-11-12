"""
同一 C++17 内核的拥有型 Python 接口；阻塞调用释放 GIL。
"""

from __future__ import annotations

import builtins
import collections.abc
import typing

__all__: builtins.list[str] = [
    "ActionMethod",
    "ActionRequest",
    "ActionResponse",
    "ActionResult",
    "ActionThenGet",
    "ActionThenGetRequest",
    "ActionThenGetResponse",
    "ActionThenGetResult",
    "ActionValue",
    "AddressType",
    "AdvancedServiceOptions",
    "AllMeters",
    "ArrayLayout",
    "AssociationParameters",
    "AttributeDefinition",
    "AttributeResult",
    "AttributeSchema",
    "AuthenticationResult",
    "BitString",
    "Boolean",
    "CandidatePoint",
    "Capabilities",
    "ChannelOptions",
    "ClientOptions",
    "ClientState",
    "Comdcb",
    "Completion",
    "ConnectRequest",
    "ConnectResponse",
    "ConnectionProfile",
    "Data",
    "DataType",
    "Date",
    "DateTime",
    "DateTimeS",
    "DemandValue",
    "Device",
    "DeviceLayout",
    "DeviceOptions",
    "Engine",
    "Enumeration",
    "Error",
    "ErrorCode",
    "ErrorResponse",
    "Event",
    "EventQueue",
    "FactoryVersion",
    "Float32",
    "Float64",
    "Fragment",
    "FragmentType",
    "Frame",
    "FrameStreamDecoder",
    "GetBlockTransfer",
    "GetMd5Request",
    "GetMd5Response",
    "GetNextRequest",
    "GetNextResponse",
    "GetRecord",
    "GetRecordRequest",
    "GetRecordResponse",
    "GetRequest",
    "GetResponse",
    "IChannel",
    "IExecutor",
    "ITimer",
    "Int16",
    "Int32",
    "Int64",
    "Int8",
    "IoRuntime",
    "Limits",
    "LinkFragmenter",
    "LinkReassembler",
    "LinkRequest",
    "LinkRequestType",
    "LinkResponse",
    "Mac",
    "ManualExecutor",
    "MemoryChannel",
    "MemoryObject",
    "MemoryOptions",
    "MemoryRecords",
    "MeterAddressRegions",
    "MeterAddresses",
    "MeterNumberRegions",
    "MeterNumbers",
    "MeterTypeRegions",
    "MeterTypes",
    "MethodSchema",
    "NativeClient",
    "NativeServer",
    "NoMeters",
    "NullSecurity",
    "Oad",
    "ObjectDefinition",
    "ObjectProvider",
    "ObjectRegistry",
    "ObjectSchema",
    "OctetString",
    "Oi",
    "Omd",
    "PasswordSecurity",
    "Phase",
    "PointResult",
    "ProbeOptions",
    "ProxyActionResultTarget",
    "ProxyActionTarget",
    "ProxyActionThenGetResultTarget",
    "ProxyActionThenGetTarget",
    "ProxyGetResultTarget",
    "ProxyGetTarget",
    "ProxyProvider",
    "ProxyRecordRequest",
    "ProxyRecordResponse",
    "ProxyRequest",
    "ProxyResponse",
    "ProxyRouter",
    "ProxySetResultTarget",
    "ProxySetTarget",
    "ProxySetThenGetResultTarget",
    "ProxySetThenGetTarget",
    "ProxyTransRequest",
    "ProxyTransResponse",
    "ReadBatch",
    "ReadService",
    "Reassembly",
    "RecordDefinition",
    "RecordLimits",
    "RecordResult",
    "Region",
    "ReleaseNotification",
    "ReleaseRequest",
    "ReleaseResponse",
    "ReportNotification",
    "ReportResponse",
    "Rn",
    "RnMac",
    "Road",
    "Role",
    "ScaledNumber",
    "ScalerUnit",
    "SecurityBackend",
    "SecurityData",
    "SecurityRequest",
    "SecurityResponse",
    "SelectAll",
    "Selector1",
    "Selector10",
    "Selector2",
    "Selector3",
    "Selector4",
    "Selector5",
    "Selector6",
    "Selector7",
    "Selector8",
    "Selector9",
    "SerialChannel",
    "SerialFlowControl",
    "SerialLinkChannel",
    "SerialLinkOptions",
    "SerialOptions",
    "SerialParity",
    "SerialStopBits",
    "ServerAddress",
    "ServerOperation",
    "ServerOptions",
    "ServerRunner",
    "ServerState",
    "SessionHandle",
    "SessionOptions",
    "SessionState",
    "SetAttribute",
    "SetRequest",
    "SetResponse",
    "SetResult",
    "SetThenGet",
    "SetThenGetRequest",
    "SetThenGetResponse",
    "SetThenGetResult",
    "Sid",
    "SidMac",
    "SignatureSecurity",
    "Support",
    "SymmetrySecurity",
    "TcpChannel",
    "TcpListener",
    "Ti",
    "Time",
    "TimeTag",
    "TransBridge",
    "TransData",
    "TransJob",
    "Tsa",
    "UInt16",
    "UInt32",
    "UInt64",
    "UInt8",
    "Utf8String",
    "ValueDefinition",
    "VisibleString",
    "Wiring",
    "advanced_capability",
    "advanced_matches",
    "approximate_value",
    "async_probe_points",
    "attach_advanced_services",
    "attach_services",
    "build_info",
    "candidate_points",
    "capabilities_from_connect",
    "crc16",
    "decimal_text",
    "decode_apdu",
    "decode_data",
    "decode_fragment",
    "decode_frame",
    "decode_security",
    "demand_oad",
    "demand_values",
    "encode_apdu",
    "encode_data",
    "encode_fragment",
    "encode_frame",
    "encode_security",
    "energy_oad",
    "engineering_values",
    "find_attribute",
    "find_object",
    "find_record",
    "harmonic_oad",
    "make_oad",
    "make_object_schema",
    "make_record_query",
    "objects",
    "phase_oad",
    "plan_reads",
    "point_support",
    "record_at",
    "record_between",
    "record_sequences",
    "register_standard_object",
    "require_record_service",
    "service_support",
    "tariff_oad",
    "unit_symbol",
    "validate_layout",
    "validate_oad",
    "validate_record_cell",
    "validate_record_query",
    "validate_record_result",
    "validate_record_time",
    "validate_session_options",
    "validate_value",
]

class ActionMethod:
    def __init__(self, *, method: Omd = ..., parameter: Data = ...) -> None: ...
    @property
    def method(self) -> Omd: ...
    @method.setter
    def method(self, arg1: Omd) -> None: ...
    @property
    def parameter(self) -> Data: ...
    @parameter.setter
    def parameter(self, arg1: Data) -> None: ...

class ActionRequest:
    def __init__(
        self,
        *,
        list: bool = ...,
        methods: builtins.list[ActionMethod] = ...,
        piid: int = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def list(self) -> bool: ...
    @list.setter
    def list(self, arg1: bool) -> None: ...
    @property
    def methods(self) -> builtins.list[ActionMethod]: ...
    @methods.setter
    def methods(self, arg1: builtins.list[ActionMethod]) -> None: ...
    @property
    def piid(self) -> int: ...
    @piid.setter
    def piid(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class ActionResponse:
    def __init__(
        self,
        *,
        follow_report: builtins.list[AttributeResult] | builtins.list[RecordResult] | None = ...,
        list: bool = ...,
        methods: builtins.list[ActionResult] = ...,
        piid_acd: int = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def follow_report(
        self,
    ) -> builtins.list[AttributeResult] | builtins.list[RecordResult] | None: ...
    @follow_report.setter
    def follow_report(
        self, arg1: builtins.list[AttributeResult] | builtins.list[RecordResult] | None
    ) -> None: ...
    @property
    def list(self) -> bool: ...
    @list.setter
    def list(self, arg1: bool) -> None: ...
    @property
    def methods(self) -> builtins.list[ActionResult]: ...
    @methods.setter
    def methods(self, arg1: builtins.list[ActionResult]) -> None: ...
    @property
    def piid_acd(self) -> int: ...
    @piid_acd.setter
    def piid_acd(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class ActionResult:
    def __init__(self, *, dar: int = ..., data: Data | None = ..., method: Omd = ...) -> None: ...
    @property
    def dar(self) -> int: ...
    @dar.setter
    def dar(self, arg1: int) -> None: ...
    @property
    def data(self) -> Data | None: ...
    @data.setter
    def data(self, arg1: Data | None) -> None: ...
    @property
    def method(self) -> Omd: ...
    @method.setter
    def method(self, arg1: Omd) -> None: ...

class ActionThenGet:
    def __init__(
        self, *, action: ActionMethod = ..., delay_seconds: int = ..., read: Oad = ...
    ) -> None: ...
    @property
    def action(self) -> ActionMethod: ...
    @action.setter
    def action(self, arg1: ActionMethod) -> None: ...
    @property
    def delay_seconds(self) -> int: ...
    @delay_seconds.setter
    def delay_seconds(self, arg1: int) -> None: ...
    @property
    def read(self) -> Oad: ...
    @read.setter
    def read(self, arg1: Oad) -> None: ...

class ActionThenGetRequest:
    def __init__(
        self,
        *,
        items: builtins.list[ActionThenGet] = ...,
        piid: int = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def items(self) -> builtins.list[ActionThenGet]: ...
    @items.setter
    def items(self, arg1: builtins.list[ActionThenGet]) -> None: ...
    @property
    def piid(self) -> int: ...
    @piid.setter
    def piid(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class ActionThenGetResponse:
    def __init__(
        self,
        *,
        follow_report: builtins.list[AttributeResult] | builtins.list[RecordResult] | None = ...,
        items: builtins.list[ActionThenGetResult] = ...,
        piid_acd: int = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def follow_report(
        self,
    ) -> builtins.list[AttributeResult] | builtins.list[RecordResult] | None: ...
    @follow_report.setter
    def follow_report(
        self, arg1: builtins.list[AttributeResult] | builtins.list[RecordResult] | None
    ) -> None: ...
    @property
    def items(self) -> builtins.list[ActionThenGetResult]: ...
    @items.setter
    def items(self, arg1: builtins.list[ActionThenGetResult]) -> None: ...
    @property
    def piid_acd(self) -> int: ...
    @piid_acd.setter
    def piid_acd(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class ActionThenGetResult:
    def __init__(self, *, action: ActionResult = ..., read: AttributeResult = ...) -> None: ...
    @property
    def action(self) -> ActionResult: ...
    @action.setter
    def action(self, arg1: ActionResult) -> None: ...
    @property
    def read(self) -> AttributeResult: ...
    @read.setter
    def read(self, arg1: AttributeResult) -> None: ...

class ActionValue:
    def __init__(self, *, dar: int = ..., data: Data | None = ...) -> None: ...
    @property
    def dar(self) -> int: ...
    @dar.setter
    def dar(self, arg1: int) -> None: ...
    @property
    def data(self) -> Data | None: ...
    @data.setter
    def data(self, arg1: Data | None) -> None: ...

class AddressType:
    """
    Members:

      single

      wildcard

      group

      broadcast
    """

    __members__: typing.ClassVar[dict[str, AddressType]]
    broadcast: typing.ClassVar[AddressType]
    group: typing.ClassVar[AddressType]
    single: typing.ClassVar[AddressType]
    wildcard: typing.ClassVar[AddressType]

    @typing.overload
    def __eq__(self, other: AddressType) -> bool: ...
    @typing.overload
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __int__(self) -> int: ...
    @typing.overload
    def __ne__(self, other: AddressType) -> bool: ...
    @typing.overload
    def __ne__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __str__(self) -> str: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class AdvancedServiceOptions:
    def __init__(
        self,
        *,
        default_proxy_timeout_seconds: int = ...,
        default_read_delay_seconds: int = ...,
        limits: Limits = ...,
        proxy: ProxyProvider | None = ...,
    ) -> None: ...
    @property
    def default_proxy_timeout_seconds(self) -> int: ...
    @default_proxy_timeout_seconds.setter
    def default_proxy_timeout_seconds(self, arg1: int) -> None: ...
    @property
    def default_read_delay_seconds(self) -> int: ...
    @default_read_delay_seconds.setter
    def default_read_delay_seconds(self, arg1: int) -> None: ...
    @property
    def limits(self) -> Limits: ...
    @limits.setter
    def limits(self, arg1: Limits) -> None: ...
    @property
    def proxy(self) -> ProxyProvider | None: ...
    @proxy.setter
    def proxy(self, arg1: ProxyProvider | None) -> None: ...

class AllMeters:
    def __init__(self) -> None: ...

class ArrayLayout:
    """
    Members:

      none

      phases

      total_phases

      total_tariffs

      harmonics

      status_words

      variable
    """

    __members__: typing.ClassVar[dict[str, ArrayLayout]]
    harmonics: typing.ClassVar[ArrayLayout]
    none: typing.ClassVar[ArrayLayout]
    phases: typing.ClassVar[ArrayLayout]
    status_words: typing.ClassVar[ArrayLayout]
    total_phases: typing.ClassVar[ArrayLayout]
    total_tariffs: typing.ClassVar[ArrayLayout]
    variable: typing.ClassVar[ArrayLayout]

    @typing.overload
    def __eq__(self, other: ArrayLayout) -> bool: ...
    @typing.overload
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __int__(self) -> int: ...
    @typing.overload
    def __ne__(self, other: ArrayLayout) -> bool: ...
    @typing.overload
    def __ne__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __str__(self) -> str: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class AssociationParameters:
    def __init__(
        self,
        *,
        apdu_bytes: int = ...,
        function: typing.Annotated[builtins.list[int], "FixedSize(16)"] = ...,
        protocol: typing.Annotated[builtins.list[int], "FixedSize(8)"] = ...,
        receive_frame_bytes: int = ...,
        receive_window: int = ...,
        send_frame_bytes: int = ...,
        timeout_seconds: int = ...,
        version: int = ...,
    ) -> None: ...
    @property
    def apdu_bytes(self) -> int: ...
    @apdu_bytes.setter
    def apdu_bytes(self, arg1: int) -> None: ...
    @property
    def function(self) -> typing.Annotated[builtins.list[int], "FixedSize(16)"]: ...
    @function.setter
    def function(self, arg1: typing.Annotated[builtins.list[int], "FixedSize(16)"]) -> None: ...
    @property
    def protocol(self) -> typing.Annotated[builtins.list[int], "FixedSize(8)"]: ...
    @protocol.setter
    def protocol(self, arg1: typing.Annotated[builtins.list[int], "FixedSize(8)"]) -> None: ...
    @property
    def receive_frame_bytes(self) -> int: ...
    @receive_frame_bytes.setter
    def receive_frame_bytes(self, arg1: int) -> None: ...
    @property
    def receive_window(self) -> int: ...
    @receive_window.setter
    def receive_window(self, arg1: int) -> None: ...
    @property
    def send_frame_bytes(self) -> int: ...
    @send_frame_bytes.setter
    def send_frame_bytes(self, arg1: int) -> None: ...
    @property
    def timeout_seconds(self) -> int: ...
    @timeout_seconds.setter
    def timeout_seconds(self, arg1: int) -> None: ...
    @property
    def version(self) -> int: ...
    @version.setter
    def version(self, arg1: int) -> None: ...

class AttributeDefinition:
    @property
    def element_definition(self) -> ValueDefinition | None: ...
    @property
    def element_type(self) -> DataType | None: ...
    @property
    def layout(self) -> ArrayLayout: ...
    @property
    def name(self) -> str: ...
    @property
    def number(self) -> int: ...
    @property
    def readable(self) -> bool: ...
    @property
    def record(self) -> bool: ...
    @property
    def scaling(self) -> ScalerUnit | None: ...
    @property
    def source(self) -> str: ...
    @property
    def type(self) -> DataType: ...
    @property
    def value_definition(self) -> ValueDefinition | None: ...
    @property
    def writable(self) -> bool: ...

class AttributeResult:
    def __init__(self, *, attribute: Oad = ..., result: int | Data = ...) -> None: ...
    @property
    def attribute(self) -> Oad: ...
    @attribute.setter
    def attribute(self, arg1: Oad) -> None: ...
    @property
    def result(self) -> int | Data: ...
    @result.setter
    def result(self, arg1: int | Data) -> None: ...

class AttributeSchema:
    def __init__(
        self,
        *,
        number: int = ...,
        readable: bool = ...,
        record: bool = ...,
        type: DataType = ...,
        writable: bool = ...,
    ) -> None: ...
    @property
    def number(self) -> int: ...
    @number.setter
    def number(self, arg1: int) -> None: ...
    @property
    def readable(self) -> bool: ...
    @readable.setter
    def readable(self, arg1: bool) -> None: ...
    @property
    def record(self) -> bool: ...
    @record.setter
    def record(self, arg1: bool) -> None: ...
    @property
    def type(self) -> DataType: ...
    @type.setter
    def type(self, arg1: DataType) -> None: ...
    @property
    def writable(self) -> bool: ...
    @writable.setter
    def writable(self, arg1: bool) -> None: ...

class AuthenticationResult:
    def __init__(self, *, result: int = ..., security: SecurityData | None = ...) -> None: ...
    @property
    def result(self) -> int: ...
    @result.setter
    def result(self, arg1: int) -> None: ...
    @property
    def security(self) -> SecurityData | None: ...
    @security.setter
    def security(self, arg1: SecurityData | None) -> None: ...

class BitString:
    def __init__(self, *, bit_count: int = ..., value: bytes = ...) -> None: ...
    @property
    def bit_count(self) -> int: ...
    @bit_count.setter
    def bit_count(self, arg1: int) -> None: ...
    @property
    def value(self) -> bytes: ...
    @value.setter
    def value(self, arg1: bytes) -> None: ...

class Boolean:
    def __init__(self, *, value: bool = ...) -> None: ...
    @property
    def value(self) -> bool: ...
    @value.setter
    def value(self, arg1: bool) -> None: ...

class CandidatePoint:
    def __init__(self, *, attribute: Oad = ..., hint: Support = ...) -> None: ...
    @property
    def attribute(self) -> Oad: ...
    @attribute.setter
    def attribute(self, arg1: Oad) -> None: ...
    @property
    def hint(self) -> Support: ...
    @hint.setter
    def hint(self, arg1: Support) -> None: ...

class Capabilities:
    def __init__(self, *, negotiated: AssociationParameters | None = ...) -> None: ...
    @property
    def negotiated(self) -> AssociationParameters | None: ...
    @negotiated.setter
    def negotiated(self, arg1: AssociationParameters | None) -> None: ...

class ChannelOptions:
    def __init__(
        self,
        *,
        max_pending_write_bytes: int = ...,
        max_pending_writes: int = ...,
        read_chunk_bytes: int = ...,
    ) -> None: ...
    @property
    def max_pending_write_bytes(self) -> int: ...
    @max_pending_write_bytes.setter
    def max_pending_write_bytes(self, arg1: int) -> None: ...
    @property
    def max_pending_writes(self) -> int: ...
    @max_pending_writes.setter
    def max_pending_writes(self, arg1: int) -> None: ...
    @property
    def read_chunk_bytes(self) -> int: ...
    @read_chunk_bytes.setter
    def read_chunk_bytes(self, arg1: int) -> None: ...

class ClientOptions:
    def __init__(
        self,
        *,
        channel: ChannelOptions = ...,
        login_timeout: float = ...,
        protocol: SessionOptions = ...,
        transport_timeout: float = ...,
    ) -> None: ...
    @property
    def channel(self) -> ChannelOptions: ...
    @channel.setter
    def channel(self, arg1: ChannelOptions) -> None: ...
    @property
    def login_timeout(self) -> float: ...
    @login_timeout.setter
    def login_timeout(self, arg1: float) -> None: ...
    @property
    def protocol(self) -> SessionOptions: ...
    @protocol.setter
    def protocol(self, arg1: SessionOptions) -> None: ...
    @property
    def transport_timeout(self) -> float: ...
    @transport_timeout.setter
    def transport_timeout(self, arg1: float) -> None: ...

class ClientState:
    """
    Members:

      disconnected

      connecting

      connected

      disconnecting
    """

    __members__: typing.ClassVar[dict[str, ClientState]]
    connected: typing.ClassVar[ClientState]
    connecting: typing.ClassVar[ClientState]
    disconnected: typing.ClassVar[ClientState]
    disconnecting: typing.ClassVar[ClientState]

    @typing.overload
    def __eq__(self, other: ClientState) -> bool: ...
    @typing.overload
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __int__(self) -> int: ...
    @typing.overload
    def __ne__(self, other: ClientState) -> bool: ...
    @typing.overload
    def __ne__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __str__(self) -> str: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class Comdcb:
    def __init__(
        self,
        *,
        baud: int = ...,
        data_bits: int = ...,
        flow_control: int = ...,
        parity: int = ...,
        stop_bits: int = ...,
    ) -> None: ...
    @property
    def baud(self) -> int: ...
    @baud.setter
    def baud(self, arg1: int) -> None: ...
    @property
    def data_bits(self) -> int: ...
    @data_bits.setter
    def data_bits(self, arg1: int) -> None: ...
    @property
    def flow_control(self) -> int: ...
    @flow_control.setter
    def flow_control(self, arg1: int) -> None: ...
    @property
    def parity(self) -> int: ...
    @parity.setter
    def parity(self, arg1: int) -> None: ...
    @property
    def stop_bits(self) -> int: ...
    @stop_bits.setter
    def stop_bits(self, arg1: int) -> None: ...

class Completion:
    def __init__(
        self,
        *,
        connection: int = ...,
        error: Error | None = ...,
        kind: str = ...,
        message: LinkRequest
        | LinkResponse
        | ConnectRequest
        | ConnectResponse
        | ReleaseRequest
        | ReleaseResponse
        | ReleaseNotification
        | ErrorResponse
        | GetRequest
        | GetResponse
        | SetRequest
        | SetResponse
        | ActionRequest
        | ActionResponse
        | GetRecordRequest
        | GetRecordResponse
        | GetNextRequest
        | GetNextResponse
        | GetMd5Request
        | GetMd5Response
        | SetThenGetRequest
        | SetThenGetResponse
        | ActionThenGetRequest
        | ActionThenGetResponse
        | ReportNotification
        | ReportResponse
        | ProxyRequest
        | ProxyResponse
        | None = ...,
        points: builtins.list[PointResult] | None = ...,
        state: SessionState | None = ...,
        token: int = ...,
        traffic: Event | None = ...,
    ) -> None: ...
    @property
    def connection(self) -> int: ...
    @connection.setter
    def connection(self, arg1: int) -> None: ...
    @property
    def error(self) -> Error | None: ...
    @error.setter
    def error(self, arg1: Error | None) -> None: ...
    @property
    def kind(self) -> str: ...
    @kind.setter
    def kind(self, arg1: str) -> None: ...
    @property
    def message(
        self,
    ) -> (
        LinkRequest
        | LinkResponse
        | ConnectRequest
        | ConnectResponse
        | ReleaseRequest
        | ReleaseResponse
        | ReleaseNotification
        | ErrorResponse
        | GetRequest
        | GetResponse
        | SetRequest
        | SetResponse
        | ActionRequest
        | ActionResponse
        | GetRecordRequest
        | GetRecordResponse
        | GetNextRequest
        | GetNextResponse
        | GetMd5Request
        | GetMd5Response
        | SetThenGetRequest
        | SetThenGetResponse
        | ActionThenGetRequest
        | ActionThenGetResponse
        | ReportNotification
        | ReportResponse
        | ProxyRequest
        | ProxyResponse
        | None
    ): ...
    @message.setter
    def message(
        self,
        arg1: LinkRequest
        | LinkResponse
        | ConnectRequest
        | ConnectResponse
        | ReleaseRequest
        | ReleaseResponse
        | ReleaseNotification
        | ErrorResponse
        | GetRequest
        | GetResponse
        | SetRequest
        | SetResponse
        | ActionRequest
        | ActionResponse
        | GetRecordRequest
        | GetRecordResponse
        | GetNextRequest
        | GetNextResponse
        | GetMd5Request
        | GetMd5Response
        | SetThenGetRequest
        | SetThenGetResponse
        | ActionThenGetRequest
        | ActionThenGetResponse
        | ReportNotification
        | ReportResponse
        | ProxyRequest
        | ProxyResponse
        | None,
    ) -> None: ...
    @property
    def points(self) -> builtins.list[PointResult] | None: ...
    @points.setter
    def points(self, arg1: builtins.list[PointResult] | None) -> None: ...
    @property
    def state(self) -> SessionState | None: ...
    @state.setter
    def state(self, arg1: SessionState | None) -> None: ...
    @property
    def token(self) -> int: ...
    @token.setter
    def token(self, arg1: int) -> None: ...
    @property
    def traffic(self) -> Event | None: ...
    @traffic.setter
    def traffic(self, arg1: Event | None) -> None: ...

class ConnectRequest:
    def __init__(
        self,
        *,
        mechanism: NullSecurity | PasswordSecurity | SymmetrySecurity | SignatureSecurity = ...,
        parameters: AssociationParameters = ...,
        piid: int = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def mechanism(
        self,
    ) -> NullSecurity | PasswordSecurity | SymmetrySecurity | SignatureSecurity: ...
    @mechanism.setter
    def mechanism(
        self, arg1: NullSecurity | PasswordSecurity | SymmetrySecurity | SignatureSecurity
    ) -> None: ...
    @property
    def parameters(self) -> AssociationParameters: ...
    @parameters.setter
    def parameters(self, arg1: AssociationParameters) -> None: ...
    @property
    def piid(self) -> int: ...
    @piid.setter
    def piid(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class ConnectResponse:
    def __init__(
        self,
        *,
        factory: FactoryVersion = ...,
        follow_report: builtins.list[AttributeResult] | builtins.list[RecordResult] | None = ...,
        parameters: AssociationParameters = ...,
        piid_acd: int = ...,
        result: int = ...,
        security: SecurityData | None = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def factory(self) -> FactoryVersion: ...
    @factory.setter
    def factory(self, arg1: FactoryVersion) -> None: ...
    @property
    def follow_report(
        self,
    ) -> builtins.list[AttributeResult] | builtins.list[RecordResult] | None: ...
    @follow_report.setter
    def follow_report(
        self, arg1: builtins.list[AttributeResult] | builtins.list[RecordResult] | None
    ) -> None: ...
    @property
    def parameters(self) -> AssociationParameters: ...
    @parameters.setter
    def parameters(self, arg1: AssociationParameters) -> None: ...
    @property
    def piid_acd(self) -> int: ...
    @piid_acd.setter
    def piid_acd(self, arg1: int) -> None: ...
    @property
    def result(self) -> int: ...
    @result.setter
    def result(self, arg1: int) -> None: ...
    @property
    def security(self) -> SecurityData | None: ...
    @security.setter
    def security(self, arg1: SecurityData | None) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class ConnectionProfile:
    """
    Members:

      remote_public

      local_public

      local_preset
    """

    __members__: typing.ClassVar[dict[str, ConnectionProfile]]
    local_preset: typing.ClassVar[ConnectionProfile]
    local_public: typing.ClassVar[ConnectionProfile]
    remote_public: typing.ClassVar[ConnectionProfile]

    @typing.overload
    def __eq__(self, other: ConnectionProfile) -> bool: ...
    @typing.overload
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __int__(self) -> int: ...
    @typing.overload
    def __ne__(self, other: ConnectionProfile) -> bool: ...
    @typing.overload
    def __ne__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __str__(self) -> str: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class Data:
    __hash__: typing.ClassVar[None] = None  # type: ignore[assignment]

    @staticmethod
    def array(arg0: collections.abc.Sequence[Data]) -> Data: ...
    @staticmethod
    def bit_string(bit_count: typing.SupportsInt | typing.SupportsIndex, value: object) -> Data: ...
    @staticmethod
    def boolean(value: bool) -> Data: ...
    @staticmethod
    def comdcb(arg0: Comdcb) -> Data: ...
    @staticmethod
    def csd(arg0: Oad | Road) -> Data: ...
    @staticmethod
    def date(value: collections.abc.Sequence[int]) -> Data: ...
    @staticmethod
    def date_time(value: collections.abc.Sequence[int]) -> Data: ...
    @staticmethod
    def date_time_s(value: collections.abc.Sequence[int]) -> Data: ...
    @staticmethod
    def enumeration(value: int) -> Data: ...
    @staticmethod
    def float32(value: float) -> Data: ...
    @staticmethod
    def float64(value: float) -> Data: ...
    @staticmethod
    def int16(value: int) -> Data: ...
    @staticmethod
    def int32(value: int) -> Data: ...
    @staticmethod
    def int64(value: int) -> Data: ...
    @staticmethod
    def int8(value: int) -> Data: ...
    @staticmethod
    def mac(value: bytes | bytearray | memoryview) -> Data: ...
    @staticmethod
    def ms(
        arg0: NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions,
    ) -> Data: ...
    @staticmethod
    def null() -> Data: ...
    @staticmethod
    def oad(arg0: Oad) -> Data: ...
    @staticmethod
    def octet_string(value: bytes | bytearray | memoryview) -> Data: ...
    @staticmethod
    def oi(value: int) -> Data: ...
    @staticmethod
    def omd(arg0: Omd) -> Data: ...
    @staticmethod
    def rcsd(arg0: collections.abc.Sequence[Oad | Road]) -> Data: ...
    @staticmethod
    def region(arg0: Region) -> Data: ...
    @staticmethod
    def rn(value: bytes | bytearray | memoryview) -> Data: ...
    @staticmethod
    def road(arg0: Road) -> Data: ...
    @staticmethod
    def rsd(
        arg0: SelectAll
        | Selector1
        | Selector2
        | Selector3
        | Selector4
        | Selector5
        | Selector6
        | Selector7
        | Selector8
        | Selector9
        | Selector10,
    ) -> Data: ...
    @staticmethod
    def scaler_unit(arg0: ScalerUnit) -> Data: ...
    @staticmethod
    def sid(arg0: Sid) -> Data: ...
    @staticmethod
    def sid_mac(arg0: SidMac) -> Data: ...
    @staticmethod
    def structure(arg0: collections.abc.Sequence[Data]) -> Data: ...
    @staticmethod
    def ti(arg0: Ti) -> Data: ...
    @staticmethod
    def time(value: collections.abc.Sequence[int]) -> Data: ...
    @staticmethod
    def tsa(value: bytes | bytearray | memoryview) -> Data: ...
    @staticmethod
    def uint16(value: int) -> Data: ...
    @staticmethod
    def uint32(value: int) -> Data: ...
    @staticmethod
    def uint64(value: int) -> Data: ...
    @staticmethod
    def uint8(value: int) -> Data: ...
    @staticmethod
    def utf8_string(value: str) -> Data: ...
    @staticmethod
    def visible_string(value: str) -> Data: ...
    def __eq__(self, arg0: object) -> bool: ...
    def __init__(self) -> None: ...
    def __repr__(self) -> str: ...
    def as_boolean(self) -> bool: ...
    def as_date(self) -> typing.Annotated[builtins.list[int], "FixedSize(5)"]: ...
    def as_date_time(self) -> typing.Annotated[builtins.list[int], "FixedSize(10)"]: ...
    def as_date_time_s(self) -> typing.Annotated[builtins.list[int], "FixedSize(7)"]: ...
    def as_enumeration(self) -> int: ...
    def as_float32(self) -> float: ...
    def as_float64(self) -> float: ...
    def as_int16(self) -> int: ...
    def as_int32(self) -> int: ...
    def as_int64(self) -> int: ...
    def as_int8(self) -> int: ...
    def as_mac(self) -> bytes: ...
    def as_octet_string(self) -> bytes: ...
    def as_oi(self) -> int: ...
    def as_rn(self) -> bytes: ...
    def as_time(self) -> typing.Annotated[builtins.list[int], "FixedSize(3)"]: ...
    def as_tsa(self) -> bytes: ...
    def as_uint16(self) -> int: ...
    def as_uint32(self) -> int: ...
    def as_uint64(self) -> int: ...
    def as_uint8(self) -> int: ...
    def as_utf8_string(self) -> str: ...
    def as_visible_string(self) -> str: ...
    @property
    def type(self) -> DataType: ...
    @property
    def value(
        self,
    ) -> (
        None
        | bool
        | int
        | float
        | str
        | bytes
        | builtins.list[Data]
        | builtins.list[int]
        | Oad
        | Omd
        | Ti
        | ScalerUnit
        | Sid
        | SidMac
        | Comdcb
        | BitString
        | Road
        | Region
        | SelectAll
        | Selector1
        | Selector2
        | Selector3
        | Selector4
        | Selector5
        | Selector6
        | Selector7
        | Selector8
        | Selector9
        | Selector10
        | NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions
        | builtins.list[Oad | Road]
    ): ...

class DataType:
    """
    Members:

      null

      array

      structure

      boolean

      bit_string

      int32

      uint32

      octet_string

      visible_string

      utf8_string

      int8

      int16

      uint8

      uint16

      int64

      uint64

      enumeration

      float32

      float64

      date_time

      date

      time

      date_time_s

      oi

      oad

      road

      omd

      ti

      tsa

      mac

      rn

      region

      scaler_unit

      rsd

      csd

      ms

      sid

      sid_mac

      comdcb

      rcsd
    """

    __members__: typing.ClassVar[dict[str, DataType]]
    array: typing.ClassVar[DataType]
    bit_string: typing.ClassVar[DataType]
    boolean: typing.ClassVar[DataType]
    comdcb: typing.ClassVar[DataType]
    csd: typing.ClassVar[DataType]
    date: typing.ClassVar[DataType]
    date_time: typing.ClassVar[DataType]
    date_time_s: typing.ClassVar[DataType]
    enumeration: typing.ClassVar[DataType]
    float32: typing.ClassVar[DataType]
    float64: typing.ClassVar[DataType]
    int16: typing.ClassVar[DataType]
    int32: typing.ClassVar[DataType]
    int64: typing.ClassVar[DataType]
    int8: typing.ClassVar[DataType]
    mac: typing.ClassVar[DataType]
    ms: typing.ClassVar[DataType]
    null: typing.ClassVar[DataType]
    oad: typing.ClassVar[DataType]
    octet_string: typing.ClassVar[DataType]
    oi: typing.ClassVar[DataType]
    omd: typing.ClassVar[DataType]
    rcsd: typing.ClassVar[DataType]
    region: typing.ClassVar[DataType]
    rn: typing.ClassVar[DataType]
    road: typing.ClassVar[DataType]
    rsd: typing.ClassVar[DataType]
    scaler_unit: typing.ClassVar[DataType]
    sid: typing.ClassVar[DataType]
    sid_mac: typing.ClassVar[DataType]
    structure: typing.ClassVar[DataType]
    ti: typing.ClassVar[DataType]
    time: typing.ClassVar[DataType]
    tsa: typing.ClassVar[DataType]
    uint16: typing.ClassVar[DataType]
    uint32: typing.ClassVar[DataType]
    uint64: typing.ClassVar[DataType]
    uint8: typing.ClassVar[DataType]
    utf8_string: typing.ClassVar[DataType]
    visible_string: typing.ClassVar[DataType]

    @typing.overload
    def __eq__(self, other: DataType) -> bool: ...
    @typing.overload
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __int__(self) -> int: ...
    @typing.overload
    def __ne__(self, other: DataType) -> bool: ...
    @typing.overload
    def __ne__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __str__(self) -> str: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class Date:
    def __init__(
        self, *, value: typing.Annotated[builtins.list[int], "FixedSize(5)"] = ...
    ) -> None: ...
    @property
    def value(self) -> typing.Annotated[builtins.list[int], "FixedSize(5)"]: ...
    @value.setter
    def value(self, arg1: typing.Annotated[builtins.list[int], "FixedSize(5)"]) -> None: ...

class DateTime:
    def __init__(
        self, *, value: typing.Annotated[builtins.list[int], "FixedSize(10)"] = ...
    ) -> None: ...
    @property
    def value(self) -> typing.Annotated[builtins.list[int], "FixedSize(10)"]: ...
    @value.setter
    def value(self, arg1: typing.Annotated[builtins.list[int], "FixedSize(10)"]) -> None: ...

class DateTimeS:
    def __init__(
        self, *, value: typing.Annotated[builtins.list[int], "FixedSize(7)"] = ...
    ) -> None: ...
    @property
    def value(self) -> typing.Annotated[builtins.list[int], "FixedSize(7)"]: ...
    @value.setter
    def value(self, arg1: typing.Annotated[builtins.list[int], "FixedSize(7)"]) -> None: ...

class DemandValue:
    def __init__(self, *, number: ScaledNumber = ..., occurred_at: DateTimeS = ...) -> None: ...
    @property
    def number(self) -> ScaledNumber: ...
    @number.setter
    def number(self, arg1: ScaledNumber) -> None: ...
    @property
    def occurred_at(self) -> DateTimeS: ...
    @occurred_at.setter
    def occurred_at(self, arg1: DateTimeS) -> None: ...

class Device:
    def __init__(self, options: DeviceOptions = ...) -> None: ...
    def define(self, arg0: ObjectSchema) -> None: ...
    def get(self, arg0: Oad) -> int | Data: ...
    def set(self, arg0: Oad, arg1: Data) -> None: ...
    def set_element(self, arg0: Oad, arg1: Data) -> None: ...

class DeviceLayout:
    def __init__(
        self, *, harmonic_order: int = ..., tariff_count: int = ..., wiring: Wiring = ...
    ) -> None: ...
    @property
    def harmonic_order(self) -> int: ...
    @harmonic_order.setter
    def harmonic_order(self, arg1: int) -> None: ...
    @property
    def tariff_count(self) -> int: ...
    @tariff_count.setter
    def tariff_count(self, arg1: int) -> None: ...
    @property
    def wiring(self) -> Wiring: ...
    @wiring.setter
    def wiring(self, arg1: Wiring) -> None: ...

class DeviceOptions:
    def __init__(
        self,
        *,
        layout: DeviceLayout = ...,
        limits: Limits = ...,
        max_attributes: int = ...,
        max_objects: int = ...,
        max_value_bytes: int = ...,
    ) -> None: ...
    @property
    def layout(self) -> DeviceLayout: ...
    @layout.setter
    def layout(self, arg1: DeviceLayout) -> None: ...
    @property
    def limits(self) -> Limits: ...
    @limits.setter
    def limits(self, arg1: Limits) -> None: ...
    @property
    def max_attributes(self) -> int: ...
    @max_attributes.setter
    def max_attributes(self, arg1: int) -> None: ...
    @property
    def max_objects(self) -> int: ...
    @max_objects.setter
    def max_objects(self, arg1: int) -> None: ...
    @property
    def max_value_bytes(self) -> int: ...
    @max_value_bytes.setter
    def max_value_bytes(self, arg1: int) -> None: ...

class Engine:
    def __init__(
        self,
        options: SessionOptions = ...,
        objects: ObjectRegistry | None = None,
        queue_limit: typing.SupportsInt | typing.SupportsIndex = 1024,
        advanced: AdvancedServiceOptions = ...,
        queue_bytes: typing.SupportsInt | typing.SupportsIndex = 16777216,
        transparent: TransBridge | None = None,
    ) -> None: ...
    def action(
        self,
        methods: collections.abc.Sequence[ActionMethod],
        list: bool = False,
        connection: typing.SupportsInt | typing.SupportsIndex = 1,
    ) -> int: ...
    def cancel(self, connection: typing.SupportsInt | typing.SupportsIndex = 1) -> None: ...
    def close(self) -> None: ...
    def connect(self, connection: typing.SupportsInt | typing.SupportsIndex = 1) -> int: ...
    def connect_tcp(
        self,
        host: str,
        port: typing.SupportsInt | typing.SupportsIndex,
        profile: ConnectionProfile = ConnectionProfile.remote_public,
        channel: ChannelOptions = ...,
        role: Role | None | None = None,
    ) -> int: ...
    def exchange(
        self,
        request: LinkRequest
        | LinkResponse
        | ConnectRequest
        | ConnectResponse
        | ReleaseRequest
        | ReleaseResponse
        | ReleaseNotification
        | ErrorResponse
        | GetRequest
        | GetResponse
        | SetRequest
        | SetResponse
        | ActionRequest
        | ActionResponse
        | GetRecordRequest
        | GetRecordResponse
        | GetNextRequest
        | GetNextResponse
        | GetMd5Request
        | GetMd5Response
        | SetThenGetRequest
        | SetThenGetResponse
        | ActionThenGetRequest
        | ActionThenGetResponse
        | ReportNotification
        | ReportResponse
        | ProxyRequest
        | ProxyResponse,
        connection: typing.SupportsInt | typing.SupportsIndex = 1,
    ) -> int: ...
    def get(
        self,
        attributes: collections.abc.Sequence[Oad],
        list: bool = False,
        connection: typing.SupportsInt | typing.SupportsIndex = 1,
    ) -> int: ...
    def get_record(
        self,
        records: collections.abc.Sequence[GetRecord],
        list: bool = False,
        connection: typing.SupportsInt | typing.SupportsIndex = 1,
    ) -> int: ...
    def link(
        self,
        type: LinkRequestType,
        heartbeat_seconds: typing.SupportsInt | typing.SupportsIndex = 0,
        connection: typing.SupportsInt | typing.SupportsIndex = 1,
    ) -> int: ...
    def listen(
        self,
        address: str,
        port: typing.SupportsInt | typing.SupportsIndex,
        profile: ConnectionProfile = ConnectionProfile.remote_public,
        max_connections: typing.SupportsInt | typing.SupportsIndex = 16,
        role: Role | None | None = None,
    ) -> int: ...
    def open_serial(
        self,
        path: str,
        serial: SerialOptions = ...,
        link: SerialLinkOptions = ...,
        profile: ConnectionProfile = ConnectionProfile.local_public,
        role: Role = ...,
    ) -> int: ...
    def poll(
        self, budget: typing.SupportsFloat | typing.SupportsIndex = 0.001
    ) -> builtins.list[Completion]: ...
    def probe_points(
        self,
        capabilities: Capabilities,
        attributes: collections.abc.Sequence[Oad],
        options: ProbeOptions = ...,
        connection: typing.SupportsInt | typing.SupportsIndex = 1,
    ) -> int: ...
    def release(self, connection: typing.SupportsInt | typing.SupportsIndex = 1) -> int: ...
    def session_at(
        self, connection: typing.SupportsInt | typing.SupportsIndex = 1
    ) -> SessionHandle: ...
    def set(
        self,
        attributes: collections.abc.Sequence[SetAttribute],
        list: bool = False,
        connection: typing.SupportsInt | typing.SupportsIndex = 1,
    ) -> int: ...

class Enumeration:
    def __init__(self, *, value: int = ...) -> None: ...
    @property
    def value(self) -> int: ...
    @value.setter
    def value(self, arg1: int) -> None: ...

class Error:
    context: str

    def __init__(
        self, *, code: ErrorCode = ..., offset: int = ..., remote_code: int | None = ...
    ) -> None: ...
    @property
    def code(self) -> ErrorCode: ...
    @code.setter
    def code(self, arg1: ErrorCode) -> None: ...
    @property
    def context_bytes(self) -> bytes: ...
    @property
    def offset(self) -> int: ...
    @offset.setter
    def offset(self, arg1: int) -> None: ...
    @property
    def remote_code(self) -> int | None: ...
    @remote_code.setter
    def remote_code(self, arg1: int | None) -> None: ...

class ErrorCode:
    """
    Members:

      need_more_data

      invalid_length

      invalid_value

      unsupported_tag

      unsupported_service

      checksum_header

      checksum_frame

      resource_limit

      trailing_data

      closed

      io_error

      timeout

      cancelled

      busy

      address_mismatch

      direction_mismatch

      not_associated

      association_failed

      remote_error
    """

    __members__: typing.ClassVar[dict[str, ErrorCode]]
    address_mismatch: typing.ClassVar[ErrorCode]
    association_failed: typing.ClassVar[ErrorCode]
    busy: typing.ClassVar[ErrorCode]
    cancelled: typing.ClassVar[ErrorCode]
    checksum_frame: typing.ClassVar[ErrorCode]
    checksum_header: typing.ClassVar[ErrorCode]
    closed: typing.ClassVar[ErrorCode]
    direction_mismatch: typing.ClassVar[ErrorCode]
    invalid_length: typing.ClassVar[ErrorCode]
    invalid_value: typing.ClassVar[ErrorCode]
    io_error: typing.ClassVar[ErrorCode]
    need_more_data: typing.ClassVar[ErrorCode]
    not_associated: typing.ClassVar[ErrorCode]
    remote_error: typing.ClassVar[ErrorCode]
    resource_limit: typing.ClassVar[ErrorCode]
    timeout: typing.ClassVar[ErrorCode]
    trailing_data: typing.ClassVar[ErrorCode]
    unsupported_service: typing.ClassVar[ErrorCode]
    unsupported_tag: typing.ClassVar[ErrorCode]

    @typing.overload
    def __eq__(self, other: ErrorCode) -> bool: ...
    @typing.overload
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __int__(self) -> int: ...
    @typing.overload
    def __ne__(self, other: ErrorCode) -> bool: ...
    @typing.overload
    def __ne__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __str__(self) -> str: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class ErrorResponse:
    def __init__(
        self,
        *,
        follow_report: builtins.list[AttributeResult] | builtins.list[RecordResult] | None = ...,
        piid: int = ...,
        server: bool = ...,
        time_tag: TimeTag | None = ...,
        type: int = ...,
    ) -> None: ...
    @property
    def follow_report(
        self,
    ) -> builtins.list[AttributeResult] | builtins.list[RecordResult] | None: ...
    @follow_report.setter
    def follow_report(
        self, arg1: builtins.list[AttributeResult] | builtins.list[RecordResult] | None
    ) -> None: ...
    @property
    def piid(self) -> int: ...
    @piid.setter
    def piid(self, arg1: int) -> None: ...
    @property
    def server(self) -> bool: ...
    @server.setter
    def server(self, arg1: bool) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...
    @property
    def type(self) -> int: ...
    @type.setter
    def type(self, arg1: int) -> None: ...

class Event:
    def __init__(
        self,
        *,
        bytes: bytes = ...,
        connection: int = ...,
        error: Error | None = ...,
        kind: str = ...,
        timestamp: float = ...,
    ) -> None: ...
    @property
    def bytes(self) -> bytes: ...
    @bytes.setter
    def bytes(self, arg1: bytes) -> None: ...
    @property
    def connection(self) -> int: ...
    @connection.setter
    def connection(self, arg1: int) -> None: ...
    @property
    def error(self) -> Error | None: ...
    @error.setter
    def error(self, arg1: Error | None) -> None: ...
    @property
    def kind(self) -> str: ...
    @kind.setter
    def kind(self, arg1: str) -> None: ...
    @property
    def timestamp(self) -> float: ...
    @timestamp.setter
    def timestamp(self, arg1: float) -> None: ...

class EventQueue:
    def __init__(
        self,
        capacity: typing.SupportsInt | typing.SupportsIndex = 256,
        byte_limit: typing.SupportsInt | typing.SupportsIndex = 1048576,
    ) -> None: ...
    def close(self) -> None: ...
    def drain(
        self, count: typing.SupportsInt | typing.SupportsIndex = 256
    ) -> builtins.list[Event]: ...
    @property
    def dropped(self) -> int: ...

class FactoryVersion:
    def __init__(
        self,
        *,
        extension: typing.Annotated[builtins.list[int], "FixedSize(8)"] = ...,
        hardware_date: typing.Annotated[builtins.list[int], "FixedSize(6)"] = ...,
        hardware_version: typing.Annotated[builtins.list[int], "FixedSize(4)"] = ...,
        manufacturer: typing.Annotated[builtins.list[int], "FixedSize(4)"] = ...,
        software_date: typing.Annotated[builtins.list[int], "FixedSize(6)"] = ...,
        software_version: typing.Annotated[builtins.list[int], "FixedSize(4)"] = ...,
    ) -> None: ...
    @property
    def extension(self) -> typing.Annotated[builtins.list[int], "FixedSize(8)"]: ...
    @extension.setter
    def extension(self, arg1: typing.Annotated[builtins.list[int], "FixedSize(8)"]) -> None: ...
    @property
    def hardware_date(self) -> typing.Annotated[builtins.list[int], "FixedSize(6)"]: ...
    @hardware_date.setter
    def hardware_date(self, arg1: typing.Annotated[builtins.list[int], "FixedSize(6)"]) -> None: ...
    @property
    def hardware_version(self) -> typing.Annotated[builtins.list[int], "FixedSize(4)"]: ...
    @hardware_version.setter
    def hardware_version(
        self, arg1: typing.Annotated[builtins.list[int], "FixedSize(4)"]
    ) -> None: ...
    @property
    def manufacturer(self) -> typing.Annotated[builtins.list[int], "FixedSize(4)"]: ...
    @manufacturer.setter
    def manufacturer(self, arg1: typing.Annotated[builtins.list[int], "FixedSize(4)"]) -> None: ...
    @property
    def software_date(self) -> typing.Annotated[builtins.list[int], "FixedSize(6)"]: ...
    @software_date.setter
    def software_date(self, arg1: typing.Annotated[builtins.list[int], "FixedSize(6)"]) -> None: ...
    @property
    def software_version(self) -> typing.Annotated[builtins.list[int], "FixedSize(4)"]: ...
    @software_version.setter
    def software_version(
        self, arg1: typing.Annotated[builtins.list[int], "FixedSize(4)"]
    ) -> None: ...

class Float32:
    def __init__(self, *, value: float = ...) -> None: ...
    @property
    def value(self) -> float: ...
    @value.setter
    def value(self, arg1: float) -> None: ...

class Float64:
    def __init__(self, *, value: float = ...) -> None: ...
    @property
    def value(self) -> float: ...
    @value.setter
    def value(self, arg1: float) -> None: ...

class Fragment:
    def __init__(
        self, *, data: bytes = ..., sequence: int = ..., type: FragmentType = ...
    ) -> None: ...
    @property
    def data(self) -> bytes: ...
    @data.setter
    def data(self, arg1: bytes) -> None: ...
    @property
    def sequence(self) -> int: ...
    @sequence.setter
    def sequence(self, arg1: int) -> None: ...
    @property
    def type(self) -> FragmentType: ...
    @type.setter
    def type(self, arg1: FragmentType) -> None: ...

class FragmentType:
    """
    Members:

      first

      last

      acknowledgement

      middle
    """

    __members__: typing.ClassVar[dict[str, FragmentType]]
    acknowledgement: typing.ClassVar[FragmentType]
    first: typing.ClassVar[FragmentType]
    last: typing.ClassVar[FragmentType]
    middle: typing.ClassVar[FragmentType]

    @typing.overload
    def __eq__(self, other: FragmentType) -> bool: ...
    @typing.overload
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __int__(self) -> int: ...
    @typing.overload
    def __ne__(self, other: FragmentType) -> bool: ...
    @typing.overload
    def __ne__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __str__(self) -> str: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class Frame:
    def __init__(
        self,
        *,
        client: int = ...,
        control: int = ...,
        payload: bytes = ...,
        server: ServerAddress = ...,
    ) -> None: ...
    @property
    def client(self) -> int: ...
    @client.setter
    def client(self, arg1: int) -> None: ...
    @property
    def control(self) -> int: ...
    @control.setter
    def control(self, arg1: int) -> None: ...
    @property
    def payload(self) -> bytes: ...
    @payload.setter
    def payload(self, arg1: bytes) -> None: ...
    @property
    def server(self) -> ServerAddress: ...
    @server.setter
    def server(self, arg1: ServerAddress) -> None: ...

class FrameStreamDecoder:
    def __init__(self, limits: Limits = ...) -> None: ...
    def feed(self, arg0: object) -> builtins.list[Frame | Error]: ...
    def reset(self) -> None: ...
    @property
    def buffered_size(self) -> int: ...

class GetBlockTransfer:
    @staticmethod
    def split(
        snapshot: GetResponse | GetRecordResponse,
        target_bytes: typing.SupportsInt | typing.SupportsIndex,
        limits: Limits = ...,
    ) -> builtins.list[GetNextResponse]: ...
    def __init__(
        self,
        piid: typing.SupportsInt | typing.SupportsIndex,
        records: bool,
        limits: Limits = ...,
        merge_record_rows: bool = True,
    ) -> None: ...
    def accept(self, block: GetNextResponse) -> GetResponse | GetRecordResponse | None: ...

class GetMd5Request:
    def __init__(
        self, *, attribute: Oad = ..., piid: int = ..., time_tag: TimeTag | None = ...
    ) -> None: ...
    @property
    def attribute(self) -> Oad: ...
    @attribute.setter
    def attribute(self, arg1: Oad) -> None: ...
    @property
    def piid(self) -> int: ...
    @piid.setter
    def piid(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class GetMd5Response:
    def __init__(
        self,
        *,
        attribute: Oad = ...,
        follow_report: builtins.list[AttributeResult] | builtins.list[RecordResult] | None = ...,
        piid_acd: int = ...,
        result: int | typing.Annotated[builtins.list[int], "FixedSize(16)"] = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def attribute(self) -> Oad: ...
    @attribute.setter
    def attribute(self, arg1: Oad) -> None: ...
    @property
    def follow_report(
        self,
    ) -> builtins.list[AttributeResult] | builtins.list[RecordResult] | None: ...
    @follow_report.setter
    def follow_report(
        self, arg1: builtins.list[AttributeResult] | builtins.list[RecordResult] | None
    ) -> None: ...
    @property
    def piid_acd(self) -> int: ...
    @piid_acd.setter
    def piid_acd(self, arg1: int) -> None: ...
    @property
    def result(self) -> int | typing.Annotated[builtins.list[int], "FixedSize(16)"]: ...
    @result.setter
    def result(self, arg1: int | typing.Annotated[builtins.list[int], "FixedSize(16)"]) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class GetNextRequest:
    def __init__(
        self, *, block: int = ..., piid: int = ..., time_tag: TimeTag | None = ...
    ) -> None: ...
    @property
    def block(self) -> int: ...
    @block.setter
    def block(self, arg1: int) -> None: ...
    @property
    def piid(self) -> int: ...
    @piid.setter
    def piid(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class GetNextResponse:
    def __init__(
        self,
        *,
        block: int = ...,
        follow_report: builtins.list[AttributeResult] | builtins.list[RecordResult] | None = ...,
        last: bool = ...,
        piid_acd: int = ...,
        result: int | builtins.list[AttributeResult] | builtins.list[RecordResult] = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def block(self) -> int: ...
    @block.setter
    def block(self, arg1: int) -> None: ...
    @property
    def follow_report(
        self,
    ) -> builtins.list[AttributeResult] | builtins.list[RecordResult] | None: ...
    @follow_report.setter
    def follow_report(
        self, arg1: builtins.list[AttributeResult] | builtins.list[RecordResult] | None
    ) -> None: ...
    @property
    def last(self) -> bool: ...
    @last.setter
    def last(self, arg1: bool) -> None: ...
    @property
    def piid_acd(self) -> int: ...
    @piid_acd.setter
    def piid_acd(self, arg1: int) -> None: ...
    @property
    def result(self) -> int | builtins.list[AttributeResult] | builtins.list[RecordResult]: ...
    @result.setter
    def result(
        self, arg1: int | builtins.list[AttributeResult] | builtins.list[RecordResult]
    ) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class GetRecord:
    def __init__(
        self,
        *,
        attribute: Oad = ...,
        columns: builtins.list[Oad | Road] = ...,
        rows: SelectAll
        | Selector1
        | Selector2
        | Selector3
        | Selector4
        | Selector5
        | Selector6
        | Selector7
        | Selector8
        | Selector9
        | Selector10 = ...,
    ) -> None: ...
    @property
    def attribute(self) -> Oad: ...
    @attribute.setter
    def attribute(self, arg1: Oad) -> None: ...
    @property
    def columns(self) -> builtins.list[Oad | Road]: ...
    @columns.setter
    def columns(self, arg1: builtins.list[Oad | Road]) -> None: ...
    @property
    def rows(
        self,
    ) -> (
        SelectAll
        | Selector1
        | Selector2
        | Selector3
        | Selector4
        | Selector5
        | Selector6
        | Selector7
        | Selector8
        | Selector9
        | Selector10
    ): ...
    @rows.setter
    def rows(
        self,
        arg1: SelectAll
        | Selector1
        | Selector2
        | Selector3
        | Selector4
        | Selector5
        | Selector6
        | Selector7
        | Selector8
        | Selector9
        | Selector10,
    ) -> None: ...

class GetRecordRequest:
    def __init__(
        self,
        *,
        list: bool = ...,
        piid: int = ...,
        records: builtins.list[GetRecord] = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def list(self) -> bool: ...
    @list.setter
    def list(self, arg1: bool) -> None: ...
    @property
    def piid(self) -> int: ...
    @piid.setter
    def piid(self, arg1: int) -> None: ...
    @property
    def records(self) -> builtins.list[GetRecord]: ...
    @records.setter
    def records(self, arg1: builtins.list[GetRecord]) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class GetRecordResponse:
    def __init__(
        self,
        *,
        follow_report: builtins.list[AttributeResult] | builtins.list[RecordResult] | None = ...,
        list: bool = ...,
        piid_acd: int = ...,
        records: builtins.list[RecordResult] = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def follow_report(
        self,
    ) -> builtins.list[AttributeResult] | builtins.list[RecordResult] | None: ...
    @follow_report.setter
    def follow_report(
        self, arg1: builtins.list[AttributeResult] | builtins.list[RecordResult] | None
    ) -> None: ...
    @property
    def list(self) -> bool: ...
    @list.setter
    def list(self, arg1: bool) -> None: ...
    @property
    def piid_acd(self) -> int: ...
    @piid_acd.setter
    def piid_acd(self, arg1: int) -> None: ...
    @property
    def records(self) -> builtins.list[RecordResult]: ...
    @records.setter
    def records(self, arg1: builtins.list[RecordResult]) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class GetRequest:
    def __init__(
        self,
        *,
        attributes: builtins.list[Oad] = ...,
        list: bool = ...,
        piid: int = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def attributes(self) -> builtins.list[Oad]: ...
    @attributes.setter
    def attributes(self, arg1: builtins.list[Oad]) -> None: ...
    @property
    def list(self) -> bool: ...
    @list.setter
    def list(self, arg1: bool) -> None: ...
    @property
    def piid(self) -> int: ...
    @piid.setter
    def piid(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class GetResponse:
    def __init__(
        self,
        *,
        attributes: builtins.list[AttributeResult] = ...,
        follow_report: builtins.list[AttributeResult] | builtins.list[RecordResult] | None = ...,
        list: bool = ...,
        piid_acd: int = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def attributes(self) -> builtins.list[AttributeResult]: ...
    @attributes.setter
    def attributes(self, arg1: builtins.list[AttributeResult]) -> None: ...
    @property
    def follow_report(
        self,
    ) -> builtins.list[AttributeResult] | builtins.list[RecordResult] | None: ...
    @follow_report.setter
    def follow_report(
        self, arg1: builtins.list[AttributeResult] | builtins.list[RecordResult] | None
    ) -> None: ...
    @property
    def list(self) -> bool: ...
    @list.setter
    def list(self, arg1: bool) -> None: ...
    @property
    def piid_acd(self) -> int: ...
    @piid_acd.setter
    def piid_acd(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class IChannel:
    def __init__(self) -> None: ...
    def async_read(self, callback: collections.abc.Callable[[bytes | Error], None]) -> None: ...
    def async_write(
        self, data: bytes, callback: collections.abc.Callable[[None | Error], None]
    ) -> None: ...
    def close(self) -> None: ...

class IExecutor:
    def __init__(self) -> None: ...
    def is_current(self) -> bool: ...
    def now(self) -> float: ...
    def post(self, task: collections.abc.Callable[[], None]) -> None: ...
    def schedule(
        self,
        delay: typing.SupportsFloat | typing.SupportsIndex,
        task: collections.abc.Callable[[], None],
    ) -> ITimer: ...

class ITimer:
    def __init__(self) -> None: ...
    def cancel(self) -> None: ...

class Int16:
    def __init__(self, *, value: int = ...) -> None: ...
    @property
    def value(self) -> int: ...
    @value.setter
    def value(self, arg1: int) -> None: ...

class Int32:
    def __init__(self, *, value: int = ...) -> None: ...
    @property
    def value(self) -> int: ...
    @value.setter
    def value(self, arg1: int) -> None: ...

class Int64:
    def __init__(self, *, value: int = ...) -> None: ...
    @property
    def value(self) -> int: ...
    @value.setter
    def value(self, arg1: int) -> None: ...

class Int8:
    def __init__(self, *, value: int = ...) -> None: ...
    @property
    def value(self) -> int: ...
    @value.setter
    def value(self, arg1: int) -> None: ...

class IoRuntime:
    def __init__(self) -> None: ...
    def executor(self) -> IExecutor: ...
    def finish(self) -> None: ...
    def restart(self) -> None: ...
    def run_for(self, budget: typing.SupportsFloat | typing.SupportsIndex = 0.001) -> None: ...
    def stop(self) -> None: ...

class Limits:
    def __init__(
        self,
        *,
        max_data_bytes: int = ...,
        max_depth: int = ...,
        max_elements: int = ...,
        max_frame_bytes: int = ...,
        max_stream_bytes: int = ...,
    ) -> None: ...
    @property
    def max_data_bytes(self) -> int: ...
    @max_data_bytes.setter
    def max_data_bytes(self, arg1: int) -> None: ...
    @property
    def max_depth(self) -> int: ...
    @max_depth.setter
    def max_depth(self, arg1: int) -> None: ...
    @property
    def max_elements(self) -> int: ...
    @max_elements.setter
    def max_elements(self, arg1: int) -> None: ...
    @property
    def max_frame_bytes(self) -> int: ...
    @max_frame_bytes.setter
    def max_frame_bytes(self, arg1: int) -> None: ...
    @property
    def max_stream_bytes(self) -> int: ...
    @max_stream_bytes.setter
    def max_stream_bytes(self, arg1: int) -> None: ...

class LinkFragmenter:
    def __init__(
        self,
        apdu: bytes,
        fragment_bytes: typing.SupportsInt | typing.SupportsIndex,
        limit: typing.SupportsInt | typing.SupportsIndex,
    ) -> None: ...
    def acknowledge(self, sequence: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def current(self) -> Fragment: ...

class LinkReassembler:
    def __init__(self, limit: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def accept(self, fragment: Fragment) -> Reassembly: ...
    def reset(self) -> None: ...
    @property
    def active(self) -> bool: ...

class LinkRequest:
    def __init__(
        self,
        *,
        heartbeat_seconds: int = ...,
        piid_acd: int = ...,
        requested_at: DateTime = ...,
        type: LinkRequestType = ...,
    ) -> None: ...
    @property
    def heartbeat_seconds(self) -> int: ...
    @heartbeat_seconds.setter
    def heartbeat_seconds(self, arg1: int) -> None: ...
    @property
    def piid_acd(self) -> int: ...
    @piid_acd.setter
    def piid_acd(self, arg1: int) -> None: ...
    @property
    def requested_at(self) -> DateTime: ...
    @requested_at.setter
    def requested_at(self, arg1: DateTime) -> None: ...
    @property
    def type(self) -> LinkRequestType: ...
    @type.setter
    def type(self, arg1: LinkRequestType) -> None: ...

class LinkRequestType:
    """
    Members:

      login

      heartbeat

      logout
    """

    __members__: typing.ClassVar[dict[str, LinkRequestType]]
    heartbeat: typing.ClassVar[LinkRequestType]
    login: typing.ClassVar[LinkRequestType]
    logout: typing.ClassVar[LinkRequestType]

    @typing.overload
    def __eq__(self, other: LinkRequestType) -> bool: ...
    @typing.overload
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __int__(self) -> int: ...
    @typing.overload
    def __ne__(self, other: LinkRequestType) -> bool: ...
    @typing.overload
    def __ne__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __str__(self) -> str: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class LinkResponse:
    def __init__(
        self,
        *,
        piid: int = ...,
        received_at: DateTime = ...,
        requested_at: DateTime = ...,
        responded_at: DateTime = ...,
        result: int = ...,
    ) -> None: ...
    @property
    def piid(self) -> int: ...
    @piid.setter
    def piid(self, arg1: int) -> None: ...
    @property
    def received_at(self) -> DateTime: ...
    @received_at.setter
    def received_at(self, arg1: DateTime) -> None: ...
    @property
    def requested_at(self) -> DateTime: ...
    @requested_at.setter
    def requested_at(self, arg1: DateTime) -> None: ...
    @property
    def responded_at(self) -> DateTime: ...
    @responded_at.setter
    def responded_at(self, arg1: DateTime) -> None: ...
    @property
    def result(self) -> int: ...
    @result.setter
    def result(self, arg1: int) -> None: ...

class Mac:
    def __init__(self, *, value: bytes = ...) -> None: ...
    @property
    def value(self) -> bytes: ...
    @value.setter
    def value(self, arg1: bytes) -> None: ...

class ManualExecutor(IExecutor):
    def __init__(self) -> None: ...
    def advance(self, elapsed: typing.SupportsFloat | typing.SupportsIndex) -> None: ...
    def run_ready(self) -> None: ...

class MemoryChannel(IChannel):
    @staticmethod
    def pair(
        executor: IExecutor, options: MemoryOptions = ...
    ) -> tuple[MemoryChannel, MemoryChannel]: ...

class MemoryObject(ObjectProvider):
    def __init__(self) -> None: ...
    def bind_method(
        self,
        method: typing.SupportsInt | typing.SupportsIndex,
        callback: collections.abc.Callable[[Omd, Data], ActionValue],
    ) -> None: ...
    def bind_record(
        self,
        attribute: typing.SupportsInt | typing.SupportsIndex,
        callback: collections.abc.Callable[[GetRecord], RecordResult],
    ) -> None: ...
    def set(self, arg0: typing.SupportsInt | typing.SupportsIndex, arg1: Data) -> None: ...

class MemoryOptions:
    def __init__(
        self,
        *,
        max_buffer_bytes: int = ...,
        max_pending_writes: int = ...,
        read_chunk_bytes: int = ...,
    ) -> None: ...
    @property
    def max_buffer_bytes(self) -> int: ...
    @max_buffer_bytes.setter
    def max_buffer_bytes(self, arg1: int) -> None: ...
    @property
    def max_pending_writes(self) -> int: ...
    @max_pending_writes.setter
    def max_pending_writes(self, arg1: int) -> None: ...
    @property
    def read_chunk_bytes(self) -> int: ...
    @read_chunk_bytes.setter
    def read_chunk_bytes(self, arg1: int) -> None: ...

class MemoryRecords(ObjectProvider):
    @staticmethod
    def create(
        oi: typing.SupportsInt | typing.SupportsIndex,
        columns: collections.abc.Sequence[Oad] = [],
        layout: DeviceLayout = ...,
        limits: Limits = ...,
        record_limits: RecordLimits = ...,
    ) -> MemoryRecords: ...
    def replace_rows(
        self, arg0: collections.abc.Sequence[collections.abc.Sequence[Data]]
    ) -> None: ...

class MeterAddressRegions:
    def __init__(self, *, values: builtins.list[Region] = ...) -> None: ...
    @property
    def values(self) -> builtins.list[Region]: ...
    @values.setter
    def values(self, arg1: builtins.list[Region]) -> None: ...

class MeterAddresses:
    def __init__(self, *, values: builtins.list[Tsa] = ...) -> None: ...
    @property
    def values(self) -> builtins.list[Tsa]: ...
    @values.setter
    def values(self, arg1: builtins.list[Tsa]) -> None: ...

class MeterNumberRegions:
    def __init__(self, *, values: builtins.list[Region] = ...) -> None: ...
    @property
    def values(self) -> builtins.list[Region]: ...
    @values.setter
    def values(self, arg1: builtins.list[Region]) -> None: ...

class MeterNumbers:
    def __init__(self, *, values: builtins.list[int] = ...) -> None: ...
    @property
    def values(self) -> builtins.list[int]: ...
    @values.setter
    def values(self, arg1: builtins.list[int]) -> None: ...

class MeterTypeRegions:
    def __init__(self, *, values: builtins.list[Region] = ...) -> None: ...
    @property
    def values(self) -> builtins.list[Region]: ...
    @values.setter
    def values(self, arg1: builtins.list[Region]) -> None: ...

class MeterTypes:
    def __init__(self, *, values: bytes = ...) -> None: ...
    @property
    def values(self) -> bytes: ...
    @values.setter
    def values(self, arg1: bytes) -> None: ...

class MethodSchema:
    def __init__(
        self,
        *,
        executable: bool = ...,
        number: int = ...,
        parameter_type: DataType | None = ...,
        return_type: DataType | None = ...,
    ) -> None: ...
    @property
    def executable(self) -> bool: ...
    @executable.setter
    def executable(self, arg1: bool) -> None: ...
    @property
    def number(self) -> int: ...
    @number.setter
    def number(self, arg1: int) -> None: ...
    @property
    def parameter_type(self) -> DataType | None: ...
    @parameter_type.setter
    def parameter_type(self, arg1: DataType | None) -> None: ...
    @property
    def return_type(self) -> DataType | None: ...
    @return_type.setter
    def return_type(self, arg1: DataType | None) -> None: ...

class NativeClient:
    def __init__(self, options: ClientOptions = ..., events: EventQueue | None = None) -> None: ...
    def action(self, arg0: Omd, arg1: Data) -> ActionValue: ...
    def action_list(self, arg0: collections.abc.Sequence[ActionMethod]) -> ActionResponse: ...
    def connect_tcp(
        self,
        host: str,
        port: typing.SupportsInt | typing.SupportsIndex,
        profile: ConnectionProfile = ConnectionProfile.remote_public,
    ) -> None: ...
    def disconnect(self) -> None: ...
    def get(self, arg0: Oad) -> int | Data: ...
    def get_list(self, arg0: collections.abc.Sequence[Oad]) -> GetResponse: ...
    def get_record(self, arg0: GetRecord) -> RecordResult: ...
    def get_record_list(self, arg0: collections.abc.Sequence[GetRecord]) -> GetRecordResponse: ...
    def open_serial(
        self,
        path: str,
        baud: typing.SupportsInt | typing.SupportsIndex = 9600,
        profile: ConnectionProfile = ConnectionProfile.local_public,
    ) -> None: ...
    def open_serial_configured(
        self,
        path: str,
        serial: SerialOptions,
        link: SerialLinkOptions,
        profile: ConnectionProfile = ConnectionProfile.local_public,
    ) -> None: ...
    def request_disconnect(self) -> None: ...
    def set(self, arg0: Oad, arg1: Data) -> int: ...
    def set_list(self, arg0: collections.abc.Sequence[SetAttribute]) -> SetResponse: ...
    @property
    def state(self) -> ClientState: ...

class NativeServer:
    def __init__(
        self,
        device: Device | None = None,
        options: ServerOptions = ...,
        events: EventQueue | None = None,
    ) -> None: ...
    def request_stop(self) -> None: ...
    def set(self, arg0: Oad, arg1: Data) -> None: ...
    def start_serial(
        self,
        path: str,
        baud: typing.SupportsInt | typing.SupportsIndex = 9600,
        profile: ConnectionProfile = ConnectionProfile.local_public,
    ) -> None: ...
    def start_serial_configured(
        self,
        path: str,
        serial: SerialOptions,
        link: SerialLinkOptions,
        profile: ConnectionProfile = ConnectionProfile.local_public,
    ) -> None: ...
    def start_tcp(
        self,
        address: str,
        port: typing.SupportsInt | typing.SupportsIndex,
        profile: ConnectionProfile = ConnectionProfile.remote_public,
    ) -> None: ...
    def stop(self) -> None: ...
    @property
    def connections(self) -> int: ...
    @property
    def device(self) -> Device: ...
    @property
    def local_port(self) -> int: ...
    @property
    def state(self) -> ServerState: ...

class NoMeters:
    def __init__(self) -> None: ...

class NullSecurity:
    def __init__(self) -> None: ...

class Oad:
    __hash__: typing.ClassVar[None] = None  # type: ignore[assignment]

    def __eq__(self, arg0: object) -> bool: ...
    def __init__(self, *, attribute: int = ..., index: int = ..., oi: int = ...) -> None: ...
    @property
    def attribute(self) -> int: ...
    @attribute.setter
    def attribute(self, arg1: int) -> None: ...
    @property
    def index(self) -> int: ...
    @index.setter
    def index(self, arg1: int) -> None: ...
    @property
    def oi(self) -> int: ...
    @oi.setter
    def oi(self, arg1: int) -> None: ...

class ObjectDefinition:
    @property
    def attributes(self) -> builtins.list[AttributeDefinition]: ...
    @property
    def class_id(self) -> int: ...
    @property
    def name(self) -> str: ...
    @property
    def oi(self) -> int: ...
    @property
    def phase(self) -> Phase | None: ...
    @property
    def source(self) -> str: ...
    @property
    def version(self) -> str: ...

class ObjectProvider:
    def __init__(self) -> None: ...
    def invoke(self, arg0: Omd, arg1: Data) -> ActionValue: ...
    def read(self, arg0: Oad) -> int | Data: ...
    def read_record(self, arg0: GetRecord) -> RecordResult: ...
    def write(self, arg0: Oad, arg1: Data) -> int: ...

class ObjectRegistry:
    def __init__(self) -> None: ...
    def invoke(self, arg0: Omd, arg1: Data) -> ActionValue: ...
    def read(self, arg0: Oad) -> int | Data: ...
    def read_record(self, arg0: GetRecord) -> RecordResult: ...
    def register_object(self, arg0: ObjectSchema, arg1: ObjectProvider) -> None: ...
    def write(self, arg0: Oad, arg1: Data) -> int: ...

class ObjectSchema:
    def __init__(
        self,
        *,
        attributes: builtins.list[AttributeSchema] = ...,
        methods: builtins.list[MethodSchema] = ...,
        name: str = ...,
        oi: int = ...,
    ) -> None: ...
    @property
    def attributes(self) -> builtins.list[AttributeSchema]: ...
    @attributes.setter
    def attributes(self, arg1: builtins.list[AttributeSchema]) -> None: ...
    @property
    def methods(self) -> builtins.list[MethodSchema]: ...
    @methods.setter
    def methods(self, arg1: builtins.list[MethodSchema]) -> None: ...
    @property
    def name(self) -> str: ...
    @name.setter
    def name(self, arg1: str) -> None: ...
    @property
    def oi(self) -> int: ...
    @oi.setter
    def oi(self, arg1: int) -> None: ...

class OctetString:
    def __init__(self, *, value: bytes = ...) -> None: ...
    @property
    def value(self) -> bytes: ...
    @value.setter
    def value(self, arg1: bytes) -> None: ...

class Oi:
    def __init__(self, *, value: int = ...) -> None: ...
    @property
    def value(self) -> int: ...
    @value.setter
    def value(self, arg1: int) -> None: ...

class Omd:
    __hash__: typing.ClassVar[None] = None  # type: ignore[assignment]

    def __eq__(self, arg0: object) -> bool: ...
    def __init__(self, *, method: int = ..., mode: int = ..., oi: int = ...) -> None: ...
    @property
    def method(self) -> int: ...
    @method.setter
    def method(self, arg1: int) -> None: ...
    @property
    def mode(self) -> int: ...
    @mode.setter
    def mode(self, arg1: int) -> None: ...
    @property
    def oi(self) -> int: ...
    @oi.setter
    def oi(self, arg1: int) -> None: ...

class PasswordSecurity:
    def __init__(self, *, password: str = ...) -> None: ...
    @property
    def password(self) -> str: ...
    @password.setter
    def password(self, arg1: str) -> None: ...

class Phase:
    """
    Members:

      total

      a

      b

      c
    """

    __members__: typing.ClassVar[dict[str, Phase]]
    a: typing.ClassVar[Phase]
    b: typing.ClassVar[Phase]
    c: typing.ClassVar[Phase]
    total: typing.ClassVar[Phase]

    @typing.overload
    def __eq__(self, other: Phase) -> bool: ...
    @typing.overload
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __int__(self) -> int: ...
    @typing.overload
    def __ne__(self, other: Phase) -> bool: ...
    @typing.overload
    def __ne__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __str__(self) -> str: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class PointResult:
    def __init__(
        self,
        *,
        attribute: Oad = ...,
        outcome: Data | int | Error = ...,
        schema_checked: bool = ...,
        validation_error: Error | None = ...,
    ) -> None: ...
    @property
    def attribute(self) -> Oad: ...
    @attribute.setter
    def attribute(self, arg1: Oad) -> None: ...
    @property
    def outcome(self) -> Data | int | Error: ...
    @outcome.setter
    def outcome(self, arg1: Data | int | Error) -> None: ...
    @property
    def schema_checked(self) -> bool: ...
    @schema_checked.setter
    def schema_checked(self, arg1: bool) -> None: ...
    @property
    def validation_error(self) -> Error | None: ...
    @validation_error.setter
    def validation_error(self, arg1: Error | None) -> None: ...

class ProbeOptions:
    def __init__(
        self, *, batch_size: int = ..., layout: DeviceLayout = ..., limits: Limits = ...
    ) -> None: ...
    @property
    def batch_size(self) -> int: ...
    @batch_size.setter
    def batch_size(self, arg1: int) -> None: ...
    @property
    def layout(self) -> DeviceLayout: ...
    @layout.setter
    def layout(self, arg1: DeviceLayout) -> None: ...
    @property
    def limits(self) -> Limits: ...
    @limits.setter
    def limits(self, arg1: Limits) -> None: ...

class ProxyActionResultTarget:
    def __init__(
        self,
        *,
        items: builtins.list[ActionResult] = ...,
        server: Tsa = ...,
        timeout_seconds: int = ...,
    ) -> None: ...
    @property
    def items(self) -> builtins.list[ActionResult]: ...
    @items.setter
    def items(self, arg1: builtins.list[ActionResult]) -> None: ...
    @property
    def server(self) -> Tsa: ...
    @server.setter
    def server(self, arg1: Tsa) -> None: ...
    @property
    def timeout_seconds(self) -> int: ...
    @timeout_seconds.setter
    def timeout_seconds(self, arg1: int) -> None: ...

class ProxyActionTarget:
    def __init__(
        self,
        *,
        items: builtins.list[ActionMethod] = ...,
        server: Tsa = ...,
        timeout_seconds: int = ...,
    ) -> None: ...
    @property
    def items(self) -> builtins.list[ActionMethod]: ...
    @items.setter
    def items(self, arg1: builtins.list[ActionMethod]) -> None: ...
    @property
    def server(self) -> Tsa: ...
    @server.setter
    def server(self, arg1: Tsa) -> None: ...
    @property
    def timeout_seconds(self) -> int: ...
    @timeout_seconds.setter
    def timeout_seconds(self, arg1: int) -> None: ...

class ProxyActionThenGetResultTarget:
    def __init__(
        self,
        *,
        items: builtins.list[ActionThenGetResult] = ...,
        server: Tsa = ...,
        timeout_seconds: int = ...,
    ) -> None: ...
    @property
    def items(self) -> builtins.list[ActionThenGetResult]: ...
    @items.setter
    def items(self, arg1: builtins.list[ActionThenGetResult]) -> None: ...
    @property
    def server(self) -> Tsa: ...
    @server.setter
    def server(self, arg1: Tsa) -> None: ...
    @property
    def timeout_seconds(self) -> int: ...
    @timeout_seconds.setter
    def timeout_seconds(self, arg1: int) -> None: ...

class ProxyActionThenGetTarget:
    def __init__(
        self,
        *,
        items: builtins.list[ActionThenGet] = ...,
        server: Tsa = ...,
        timeout_seconds: int = ...,
    ) -> None: ...
    @property
    def items(self) -> builtins.list[ActionThenGet]: ...
    @items.setter
    def items(self, arg1: builtins.list[ActionThenGet]) -> None: ...
    @property
    def server(self) -> Tsa: ...
    @server.setter
    def server(self, arg1: Tsa) -> None: ...
    @property
    def timeout_seconds(self) -> int: ...
    @timeout_seconds.setter
    def timeout_seconds(self, arg1: int) -> None: ...

class ProxyGetResultTarget:
    def __init__(
        self,
        *,
        items: builtins.list[AttributeResult] = ...,
        server: Tsa = ...,
        timeout_seconds: int = ...,
    ) -> None: ...
    @property
    def items(self) -> builtins.list[AttributeResult]: ...
    @items.setter
    def items(self, arg1: builtins.list[AttributeResult]) -> None: ...
    @property
    def server(self) -> Tsa: ...
    @server.setter
    def server(self, arg1: Tsa) -> None: ...
    @property
    def timeout_seconds(self) -> int: ...
    @timeout_seconds.setter
    def timeout_seconds(self, arg1: int) -> None: ...

class ProxyGetTarget:
    def __init__(
        self, *, items: builtins.list[Oad] = ..., server: Tsa = ..., timeout_seconds: int = ...
    ) -> None: ...
    @property
    def items(self) -> builtins.list[Oad]: ...
    @items.setter
    def items(self, arg1: builtins.list[Oad]) -> None: ...
    @property
    def server(self) -> Tsa: ...
    @server.setter
    def server(self, arg1: Tsa) -> None: ...
    @property
    def timeout_seconds(self) -> int: ...
    @timeout_seconds.setter
    def timeout_seconds(self, arg1: int) -> None: ...

class ProxyProvider:
    def __init__(self) -> None: ...
    def async_request(
        self,
        server: Tsa,
        request: LinkRequest
        | LinkResponse
        | ConnectRequest
        | ConnectResponse
        | ReleaseRequest
        | ReleaseResponse
        | ReleaseNotification
        | ErrorResponse
        | GetRequest
        | GetResponse
        | SetRequest
        | SetResponse
        | ActionRequest
        | ActionResponse
        | GetRecordRequest
        | GetRecordResponse
        | GetNextRequest
        | GetNextResponse
        | GetMd5Request
        | GetMd5Response
        | SetThenGetRequest
        | SetThenGetResponse
        | ActionThenGetRequest
        | ActionThenGetResponse
        | ReportNotification
        | ReportResponse
        | ProxyRequest
        | ProxyResponse,
        callback: collections.abc.Callable[
            [
                LinkRequest
                | LinkResponse
                | ConnectRequest
                | ConnectResponse
                | ReleaseRequest
                | ReleaseResponse
                | ReleaseNotification
                | ErrorResponse
                | GetRequest
                | GetResponse
                | SetRequest
                | SetResponse
                | ActionRequest
                | ActionResponse
                | GetRecordRequest
                | GetRecordResponse
                | GetNextRequest
                | GetNextResponse
                | GetMd5Request
                | GetMd5Response
                | SetThenGetRequest
                | SetThenGetResponse
                | ActionThenGetRequest
                | ActionThenGetResponse
                | ReportNotification
                | ReportResponse
                | ProxyRequest
                | ProxyResponse
                | Error
            ],
            None,
        ],
    ) -> collections.abc.Callable[[], None] | None: ...

class ProxyRecordRequest:
    def __init__(self, *, record: GetRecord = ..., server: Tsa = ...) -> None: ...
    @property
    def record(self) -> GetRecord: ...
    @record.setter
    def record(self, arg1: GetRecord) -> None: ...
    @property
    def server(self) -> Tsa: ...
    @server.setter
    def server(self, arg1: Tsa) -> None: ...

class ProxyRecordResponse:
    def __init__(self, *, record: RecordResult = ..., server: Tsa = ...) -> None: ...
    @property
    def record(self) -> RecordResult: ...
    @record.setter
    def record(self, arg1: RecordResult) -> None: ...
    @property
    def server(self) -> Tsa: ...
    @server.setter
    def server(self, arg1: Tsa) -> None: ...

class ProxyRequest:
    def __init__(
        self,
        *,
        payload: builtins.list[ProxyGetTarget]
        | ProxyRecordRequest
        | builtins.list[ProxySetTarget]
        | builtins.list[ProxySetThenGetTarget]
        | builtins.list[ProxyActionTarget]
        | builtins.list[ProxyActionThenGetTarget]
        | ProxyTransRequest = ...,
        piid: int = ...,
        time_tag: TimeTag | None = ...,
        timeout_seconds: int = ...,
    ) -> None: ...
    @property
    def payload(
        self,
    ) -> (
        builtins.list[ProxyGetTarget]
        | ProxyRecordRequest
        | builtins.list[ProxySetTarget]
        | builtins.list[ProxySetThenGetTarget]
        | builtins.list[ProxyActionTarget]
        | builtins.list[ProxyActionThenGetTarget]
        | ProxyTransRequest
    ): ...
    @payload.setter
    def payload(
        self,
        arg1: builtins.list[ProxyGetTarget]
        | ProxyRecordRequest
        | builtins.list[ProxySetTarget]
        | builtins.list[ProxySetThenGetTarget]
        | builtins.list[ProxyActionTarget]
        | builtins.list[ProxyActionThenGetTarget]
        | ProxyTransRequest,
    ) -> None: ...
    @property
    def piid(self) -> int: ...
    @piid.setter
    def piid(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...
    @property
    def timeout_seconds(self) -> int: ...
    @timeout_seconds.setter
    def timeout_seconds(self, arg1: int) -> None: ...

class ProxyResponse:
    def __init__(
        self,
        *,
        follow_report: builtins.list[AttributeResult] | builtins.list[RecordResult] | None = ...,
        payload: builtins.list[ProxyGetResultTarget]
        | ProxyRecordResponse
        | builtins.list[ProxySetResultTarget]
        | builtins.list[ProxySetThenGetResultTarget]
        | builtins.list[ProxyActionResultTarget]
        | builtins.list[ProxyActionThenGetResultTarget]
        | ProxyTransResponse = ...,
        piid_acd: int = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def follow_report(
        self,
    ) -> builtins.list[AttributeResult] | builtins.list[RecordResult] | None: ...
    @follow_report.setter
    def follow_report(
        self, arg1: builtins.list[AttributeResult] | builtins.list[RecordResult] | None
    ) -> None: ...
    @property
    def payload(
        self,
    ) -> (
        builtins.list[ProxyGetResultTarget]
        | ProxyRecordResponse
        | builtins.list[ProxySetResultTarget]
        | builtins.list[ProxySetThenGetResultTarget]
        | builtins.list[ProxyActionResultTarget]
        | builtins.list[ProxyActionThenGetResultTarget]
        | ProxyTransResponse
    ): ...
    @payload.setter
    def payload(
        self,
        arg1: builtins.list[ProxyGetResultTarget]
        | ProxyRecordResponse
        | builtins.list[ProxySetResultTarget]
        | builtins.list[ProxySetThenGetResultTarget]
        | builtins.list[ProxyActionResultTarget]
        | builtins.list[ProxyActionThenGetResultTarget]
        | ProxyTransResponse,
    ) -> None: ...
    @property
    def piid_acd(self) -> int: ...
    @piid_acd.setter
    def piid_acd(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class ProxyRouter(ProxyProvider):
    def __init__(self) -> None: ...
    def bind(self, arg0: Tsa, arg1: SessionHandle) -> None: ...

class ProxySetResultTarget:
    def __init__(
        self,
        *,
        items: builtins.list[SetResult] = ...,
        server: Tsa = ...,
        timeout_seconds: int = ...,
    ) -> None: ...
    @property
    def items(self) -> builtins.list[SetResult]: ...
    @items.setter
    def items(self, arg1: builtins.list[SetResult]) -> None: ...
    @property
    def server(self) -> Tsa: ...
    @server.setter
    def server(self, arg1: Tsa) -> None: ...
    @property
    def timeout_seconds(self) -> int: ...
    @timeout_seconds.setter
    def timeout_seconds(self, arg1: int) -> None: ...

class ProxySetTarget:
    def __init__(
        self,
        *,
        items: builtins.list[SetAttribute] = ...,
        server: Tsa = ...,
        timeout_seconds: int = ...,
    ) -> None: ...
    @property
    def items(self) -> builtins.list[SetAttribute]: ...
    @items.setter
    def items(self, arg1: builtins.list[SetAttribute]) -> None: ...
    @property
    def server(self) -> Tsa: ...
    @server.setter
    def server(self, arg1: Tsa) -> None: ...
    @property
    def timeout_seconds(self) -> int: ...
    @timeout_seconds.setter
    def timeout_seconds(self, arg1: int) -> None: ...

class ProxySetThenGetResultTarget:
    def __init__(
        self,
        *,
        items: builtins.list[SetThenGetResult] = ...,
        server: Tsa = ...,
        timeout_seconds: int = ...,
    ) -> None: ...
    @property
    def items(self) -> builtins.list[SetThenGetResult]: ...
    @items.setter
    def items(self, arg1: builtins.list[SetThenGetResult]) -> None: ...
    @property
    def server(self) -> Tsa: ...
    @server.setter
    def server(self, arg1: Tsa) -> None: ...
    @property
    def timeout_seconds(self) -> int: ...
    @timeout_seconds.setter
    def timeout_seconds(self, arg1: int) -> None: ...

class ProxySetThenGetTarget:
    def __init__(
        self,
        *,
        items: builtins.list[SetThenGet] = ...,
        server: Tsa = ...,
        timeout_seconds: int = ...,
    ) -> None: ...
    @property
    def items(self) -> builtins.list[SetThenGet]: ...
    @items.setter
    def items(self, arg1: builtins.list[SetThenGet]) -> None: ...
    @property
    def server(self) -> Tsa: ...
    @server.setter
    def server(self, arg1: Tsa) -> None: ...
    @property
    def timeout_seconds(self) -> int: ...
    @timeout_seconds.setter
    def timeout_seconds(self, arg1: int) -> None: ...

class ProxyTransRequest:
    def __init__(
        self,
        *,
        byte_timeout_milliseconds: int = ...,
        command: bytes = ...,
        communication: Comdcb = ...,
        port: Oad = ...,
        response_timeout_seconds: int = ...,
    ) -> None: ...
    @property
    def byte_timeout_milliseconds(self) -> int: ...
    @byte_timeout_milliseconds.setter
    def byte_timeout_milliseconds(self, arg1: int) -> None: ...
    @property
    def command(self) -> bytes: ...
    @command.setter
    def command(self, arg1: bytes) -> None: ...
    @property
    def communication(self) -> Comdcb: ...
    @communication.setter
    def communication(self, arg1: Comdcb) -> None: ...
    @property
    def port(self) -> Oad: ...
    @port.setter
    def port(self, arg1: Oad) -> None: ...
    @property
    def response_timeout_seconds(self) -> int: ...
    @response_timeout_seconds.setter
    def response_timeout_seconds(self, arg1: int) -> None: ...

class ProxyTransResponse:
    def __init__(self, *, port: Oad = ..., result: int | bytes = ...) -> None: ...
    @property
    def port(self) -> Oad: ...
    @port.setter
    def port(self, arg1: Oad) -> None: ...
    @property
    def result(self) -> int | bytes: ...
    @result.setter
    def result(self, arg1: int | bytes) -> None: ...

class ReadBatch:
    def __init__(self, *, attributes: builtins.list[Oad] = ..., list: bool = ...) -> None: ...
    @property
    def attributes(self) -> builtins.list[Oad]: ...
    @attributes.setter
    def attributes(self, arg1: builtins.list[Oad]) -> None: ...
    @property
    def list(self) -> bool: ...
    @list.setter
    def list(self, arg1: bool) -> None: ...

class ReadService:
    """
    Members:

      normal

      list

      record

      next
    """

    __members__: typing.ClassVar[dict[str, ReadService]]
    list: typing.ClassVar[ReadService]
    next: typing.ClassVar[ReadService]
    normal: typing.ClassVar[ReadService]
    record: typing.ClassVar[ReadService]

    @typing.overload
    def __eq__(self, other: ReadService) -> bool: ...
    @typing.overload
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __int__(self) -> int: ...
    @typing.overload
    def __ne__(self, other: ReadService) -> bool: ...
    @typing.overload
    def __ne__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __str__(self) -> str: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class Reassembly:
    def __init__(
        self, *, acknowledge: int | None = ..., apdu: bytes | None = ..., duplicate: bool = ...
    ) -> None: ...
    @property
    def acknowledge(self) -> int | None: ...
    @acknowledge.setter
    def acknowledge(self, arg1: int | None) -> None: ...
    @property
    def apdu(self) -> bytes | None: ...
    @apdu.setter
    def apdu(self, arg1: bytes | None) -> None: ...
    @property
    def duplicate(self) -> bool: ...
    @duplicate.setter
    def duplicate(self, arg1: bool) -> None: ...

class RecordDefinition:
    @property
    def base_columns(self) -> builtins.list[Oad]: ...
    @property
    def name(self) -> str: ...
    @property
    def oi(self) -> int: ...
    @property
    def sequence(self) -> Oad: ...
    @property
    def time(self) -> Oad: ...

class RecordLimits:
    def __init__(
        self,
        *,
        max_columns: int = ...,
        max_result_rows: int = ...,
        max_rows: int = ...,
        max_snapshot_bytes: int = ...,
    ) -> None: ...
    @property
    def max_columns(self) -> int: ...
    @max_columns.setter
    def max_columns(self, arg1: int) -> None: ...
    @property
    def max_result_rows(self) -> int: ...
    @max_result_rows.setter
    def max_result_rows(self, arg1: int) -> None: ...
    @property
    def max_rows(self) -> int: ...
    @max_rows.setter
    def max_rows(self, arg1: int) -> None: ...
    @property
    def max_snapshot_bytes(self) -> int: ...
    @max_snapshot_bytes.setter
    def max_snapshot_bytes(self, arg1: int) -> None: ...

class RecordResult:
    def __init__(
        self,
        *,
        attribute: Oad = ...,
        columns: builtins.list[Oad | Road] = ...,
        result: int | builtins.list[builtins.list[Data]] = ...,
    ) -> None: ...
    @property
    def attribute(self) -> Oad: ...
    @attribute.setter
    def attribute(self, arg1: Oad) -> None: ...
    @property
    def columns(self) -> builtins.list[Oad | Road]: ...
    @columns.setter
    def columns(self, arg1: builtins.list[Oad | Road]) -> None: ...
    @property
    def result(self) -> int | builtins.list[builtins.list[Data]]: ...
    @result.setter
    def result(self, arg1: int | builtins.list[builtins.list[Data]]) -> None: ...

class Region:
    def __init__(self, *, begin: Data = ..., boundary: int = ..., end: Data = ...) -> None: ...
    @property
    def begin(self) -> Data: ...
    @begin.setter
    def begin(self, arg1: Data) -> None: ...
    @property
    def boundary(self) -> int: ...
    @boundary.setter
    def boundary(self, arg1: int) -> None: ...
    @property
    def end(self) -> Data: ...
    @end.setter
    def end(self, arg1: Data) -> None: ...

class ReleaseNotification:
    def __init__(
        self,
        *,
        current_time: DateTimeS = ...,
        established_at: DateTimeS = ...,
        follow_report: builtins.list[AttributeResult] | builtins.list[RecordResult] | None = ...,
        piid_acd: int = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def current_time(self) -> DateTimeS: ...
    @current_time.setter
    def current_time(self, arg1: DateTimeS) -> None: ...
    @property
    def established_at(self) -> DateTimeS: ...
    @established_at.setter
    def established_at(self, arg1: DateTimeS) -> None: ...
    @property
    def follow_report(
        self,
    ) -> builtins.list[AttributeResult] | builtins.list[RecordResult] | None: ...
    @follow_report.setter
    def follow_report(
        self, arg1: builtins.list[AttributeResult] | builtins.list[RecordResult] | None
    ) -> None: ...
    @property
    def piid_acd(self) -> int: ...
    @piid_acd.setter
    def piid_acd(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class ReleaseRequest:
    def __init__(self, *, piid: int = ..., time_tag: TimeTag | None = ...) -> None: ...
    @property
    def piid(self) -> int: ...
    @piid.setter
    def piid(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class ReleaseResponse:
    def __init__(
        self,
        *,
        follow_report: builtins.list[AttributeResult] | builtins.list[RecordResult] | None = ...,
        piid_acd: int = ...,
        result: int = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def follow_report(
        self,
    ) -> builtins.list[AttributeResult] | builtins.list[RecordResult] | None: ...
    @follow_report.setter
    def follow_report(
        self, arg1: builtins.list[AttributeResult] | builtins.list[RecordResult] | None
    ) -> None: ...
    @property
    def piid_acd(self) -> int: ...
    @piid_acd.setter
    def piid_acd(self, arg1: int) -> None: ...
    @property
    def result(self) -> int: ...
    @result.setter
    def result(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class ReportNotification:
    def __init__(
        self,
        *,
        follow_report: builtins.list[AttributeResult] | builtins.list[RecordResult] | None = ...,
        payload: builtins.list[AttributeResult] | builtins.list[RecordResult] | TransData = ...,
        piid_acd: int = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def follow_report(
        self,
    ) -> builtins.list[AttributeResult] | builtins.list[RecordResult] | None: ...
    @follow_report.setter
    def follow_report(
        self, arg1: builtins.list[AttributeResult] | builtins.list[RecordResult] | None
    ) -> None: ...
    @property
    def payload(
        self,
    ) -> builtins.list[AttributeResult] | builtins.list[RecordResult] | TransData: ...
    @payload.setter
    def payload(
        self, arg1: builtins.list[AttributeResult] | builtins.list[RecordResult] | TransData
    ) -> None: ...
    @property
    def piid_acd(self) -> int: ...
    @piid_acd.setter
    def piid_acd(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class ReportResponse:
    def __init__(
        self,
        *,
        attributes: builtins.list[Oad] = ...,
        choice: int = ...,
        piid: int = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def attributes(self) -> builtins.list[Oad]: ...
    @attributes.setter
    def attributes(self, arg1: builtins.list[Oad]) -> None: ...
    @property
    def choice(self) -> int: ...
    @choice.setter
    def choice(self, arg1: int) -> None: ...
    @property
    def piid(self) -> int: ...
    @piid.setter
    def piid(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class Rn:
    def __init__(self, *, value: bytes = ...) -> None: ...
    @property
    def value(self) -> bytes: ...
    @value.setter
    def value(self, arg1: bytes) -> None: ...

class RnMac:
    def __init__(self, *, mac: Mac = ..., random: Rn = ...) -> None: ...
    @property
    def mac(self) -> Mac: ...
    @mac.setter
    def mac(self, arg1: Mac) -> None: ...
    @property
    def random(self) -> Rn: ...
    @random.setter
    def random(self, arg1: Rn) -> None: ...

class Road:
    def __init__(self, *, associated: builtins.list[Oad] = ..., attribute: Oad = ...) -> None: ...
    @property
    def associated(self) -> builtins.list[Oad]: ...
    @associated.setter
    def associated(self, arg1: builtins.list[Oad]) -> None: ...
    @property
    def attribute(self) -> Oad: ...
    @attribute.setter
    def attribute(self, arg1: Oad) -> None: ...

class Role:
    """
    Members:

      client

      server
    """

    __members__: typing.ClassVar[dict[str, Role]]
    client: typing.ClassVar[Role]
    server: typing.ClassVar[Role]

    @typing.overload
    def __eq__(self, other: Role) -> bool: ...
    @typing.overload
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __int__(self) -> int: ...
    @typing.overload
    def __ne__(self, other: Role) -> bool: ...
    @typing.overload
    def __ne__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __str__(self) -> str: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class ScaledNumber:
    def __init__(self, *, raw: int | int = ..., scaling: ScalerUnit = ...) -> None: ...
    @property
    def raw(self) -> int | int: ...
    @raw.setter
    def raw(self, arg1: int | int) -> None: ...
    @property
    def scaling(self) -> ScalerUnit: ...
    @scaling.setter
    def scaling(self, arg1: ScalerUnit) -> None: ...

class ScalerUnit:
    def __init__(self, *, scaler: int = ..., unit: int = ...) -> None: ...
    @property
    def scaler(self) -> int: ...
    @scaler.setter
    def scaler(self, arg1: int) -> None: ...
    @property
    def unit(self) -> int: ...
    @unit.setter
    def unit(self, arg1: int) -> None: ...

class SecurityBackend:
    def __init__(self) -> None: ...

class SecurityData:
    def __init__(self, *, random: bytes = ..., signature: bytes = ...) -> None: ...
    @property
    def random(self) -> bytes: ...
    @random.setter
    def random(self, arg1: bytes) -> None: ...
    @property
    def signature(self) -> bytes: ...
    @signature.setter
    def signature(self, arg1: bytes) -> None: ...

class SecurityRequest:
    def __init__(
        self,
        *,
        application: bytes = ...,
        encrypted: bool = ...,
        verification: SidMac | Rn | RnMac | Sid = ...,
    ) -> None: ...
    @property
    def application(self) -> bytes: ...
    @application.setter
    def application(self, arg1: bytes) -> None: ...
    @property
    def encrypted(self) -> bool: ...
    @encrypted.setter
    def encrypted(self, arg1: bool) -> None: ...
    @property
    def verification(self) -> SidMac | Rn | RnMac | Sid: ...
    @verification.setter
    def verification(self, arg1: SidMac | Rn | RnMac | Sid) -> None: ...

class SecurityResponse:
    def __init__(
        self,
        *,
        application: bytes | int = ...,
        encrypted: bool = ...,
        verification: Mac | SidMac | None = ...,
    ) -> None: ...
    @property
    def application(self) -> bytes | int: ...
    @application.setter
    def application(self, arg1: bytes | int) -> None: ...
    @property
    def encrypted(self) -> bool: ...
    @encrypted.setter
    def encrypted(self, arg1: bool) -> None: ...
    @property
    def verification(self) -> Mac | SidMac | None: ...
    @verification.setter
    def verification(self, arg1: Mac | SidMac | None) -> None: ...

class SelectAll:
    def __init__(self) -> None: ...

class Selector1:
    def __init__(self, *, attribute: Oad = ..., value: Data = ...) -> None: ...
    @property
    def attribute(self) -> Oad: ...
    @attribute.setter
    def attribute(self, arg1: Oad) -> None: ...
    @property
    def value(self) -> Data: ...
    @value.setter
    def value(self, arg1: Data) -> None: ...

class Selector10:
    def __init__(
        self,
        *,
        latest: int = ...,
        meters: NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions = ...,
    ) -> None: ...
    @property
    def latest(self) -> int: ...
    @latest.setter
    def latest(self, arg1: int) -> None: ...
    @property
    def meters(
        self,
    ) -> (
        NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions
    ): ...
    @meters.setter
    def meters(
        self,
        arg1: NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions,
    ) -> None: ...

class Selector2:
    def __init__(
        self, *, attribute: Oad = ..., begin: Data = ..., end: Data = ..., interval: Data = ...
    ) -> None: ...
    @property
    def attribute(self) -> Oad: ...
    @attribute.setter
    def attribute(self, arg1: Oad) -> None: ...
    @property
    def begin(self) -> Data: ...
    @begin.setter
    def begin(self, arg1: Data) -> None: ...
    @property
    def end(self) -> Data: ...
    @end.setter
    def end(self, arg1: Data) -> None: ...
    @property
    def interval(self) -> Data: ...
    @interval.setter
    def interval(self, arg1: Data) -> None: ...

class Selector3:
    def __init__(self, *, ranges: builtins.list[Selector2] = ...) -> None: ...
    @property
    def ranges(self) -> builtins.list[Selector2]: ...
    @ranges.setter
    def ranges(self, arg1: builtins.list[Selector2]) -> None: ...

class Selector4:
    def __init__(
        self,
        *,
        meters: NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions = ...,
        time: DateTimeS = ...,
    ) -> None: ...
    @property
    def meters(
        self,
    ) -> (
        NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions
    ): ...
    @meters.setter
    def meters(
        self,
        arg1: NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions,
    ) -> None: ...
    @property
    def time(self) -> DateTimeS: ...
    @time.setter
    def time(self, arg1: DateTimeS) -> None: ...

class Selector5:
    def __init__(
        self,
        *,
        meters: NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions = ...,
        time: DateTimeS = ...,
    ) -> None: ...
    @property
    def meters(
        self,
    ) -> (
        NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions
    ): ...
    @meters.setter
    def meters(
        self,
        arg1: NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions,
    ) -> None: ...
    @property
    def time(self) -> DateTimeS: ...
    @time.setter
    def time(self, arg1: DateTimeS) -> None: ...

class Selector6:
    def __init__(
        self,
        *,
        begin: DateTimeS = ...,
        end: DateTimeS = ...,
        interval: Ti = ...,
        meters: NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions = ...,
    ) -> None: ...
    @property
    def begin(self) -> DateTimeS: ...
    @begin.setter
    def begin(self, arg1: DateTimeS) -> None: ...
    @property
    def end(self) -> DateTimeS: ...
    @end.setter
    def end(self, arg1: DateTimeS) -> None: ...
    @property
    def interval(self) -> Ti: ...
    @interval.setter
    def interval(self, arg1: Ti) -> None: ...
    @property
    def meters(
        self,
    ) -> (
        NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions
    ): ...
    @meters.setter
    def meters(
        self,
        arg1: NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions,
    ) -> None: ...

class Selector7:
    def __init__(
        self,
        *,
        begin: DateTimeS = ...,
        end: DateTimeS = ...,
        interval: Ti = ...,
        meters: NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions = ...,
    ) -> None: ...
    @property
    def begin(self) -> DateTimeS: ...
    @begin.setter
    def begin(self, arg1: DateTimeS) -> None: ...
    @property
    def end(self) -> DateTimeS: ...
    @end.setter
    def end(self, arg1: DateTimeS) -> None: ...
    @property
    def interval(self) -> Ti: ...
    @interval.setter
    def interval(self, arg1: Ti) -> None: ...
    @property
    def meters(
        self,
    ) -> (
        NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions
    ): ...
    @meters.setter
    def meters(
        self,
        arg1: NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions,
    ) -> None: ...

class Selector8:
    def __init__(
        self,
        *,
        begin: DateTimeS = ...,
        end: DateTimeS = ...,
        interval: Ti = ...,
        meters: NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions = ...,
    ) -> None: ...
    @property
    def begin(self) -> DateTimeS: ...
    @begin.setter
    def begin(self, arg1: DateTimeS) -> None: ...
    @property
    def end(self) -> DateTimeS: ...
    @end.setter
    def end(self, arg1: DateTimeS) -> None: ...
    @property
    def interval(self) -> Ti: ...
    @interval.setter
    def interval(self, arg1: Ti) -> None: ...
    @property
    def meters(
        self,
    ) -> (
        NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions
    ): ...
    @meters.setter
    def meters(
        self,
        arg1: NoMeters
        | AllMeters
        | MeterTypes
        | MeterAddresses
        | MeterNumbers
        | MeterTypeRegions
        | MeterAddressRegions
        | MeterNumberRegions,
    ) -> None: ...

class Selector9:
    def __init__(self, *, previous: int = ...) -> None: ...
    @property
    def previous(self) -> int: ...
    @previous.setter
    def previous(self, arg1: int) -> None: ...

class SerialChannel(IChannel):
    @staticmethod
    def open(runtime: IoRuntime, path: str, options: SerialOptions = ...) -> SerialChannel: ...

class SerialFlowControl:
    """
    Members:

      none

      software

      hardware
    """

    __members__: typing.ClassVar[dict[str, SerialFlowControl]]
    hardware: typing.ClassVar[SerialFlowControl]
    none: typing.ClassVar[SerialFlowControl]
    software: typing.ClassVar[SerialFlowControl]

    @typing.overload
    def __eq__(self, other: SerialFlowControl) -> bool: ...
    @typing.overload
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __int__(self) -> int: ...
    @typing.overload
    def __ne__(self, other: SerialFlowControl) -> bool: ...
    @typing.overload
    def __ne__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __str__(self) -> str: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class SerialLinkChannel(IChannel):
    @staticmethod
    def wrap(
        raw: IChannel, executor: IExecutor, options: SerialLinkOptions = ...
    ) -> SerialLinkChannel: ...

class SerialLinkOptions:
    def __init__(
        self,
        *,
        async_drain: collections.abc.Callable[
            [collections.abc.Callable[[Error | None], None]], None
        ]
        | None = ...,
        baud_rate: int = ...,
        bits_per_character: int = ...,
        max_pending_write_bytes: int = ...,
        max_pending_writes: int = ...,
        set_transmit: collections.abc.Callable[[bool], Error | None] | None = ...,
    ) -> None: ...
    @property
    def async_drain(
        self,
    ) -> (
        collections.abc.Callable[[collections.abc.Callable[[Error | None], None]], None] | None
    ): ...
    @async_drain.setter
    def async_drain(
        self,
        arg1: collections.abc.Callable[[collections.abc.Callable[[Error | None], None]], None]
        | None,
    ) -> None: ...
    @property
    def baud_rate(self) -> int: ...
    @baud_rate.setter
    def baud_rate(self, arg1: int) -> None: ...
    @property
    def bits_per_character(self) -> int: ...
    @bits_per_character.setter
    def bits_per_character(self, arg1: int) -> None: ...
    @property
    def max_pending_write_bytes(self) -> int: ...
    @max_pending_write_bytes.setter
    def max_pending_write_bytes(self, arg1: int) -> None: ...
    @property
    def max_pending_writes(self) -> int: ...
    @max_pending_writes.setter
    def max_pending_writes(self, arg1: int) -> None: ...
    @property
    def set_transmit(self) -> collections.abc.Callable[[bool], Error | None] | None: ...
    @set_transmit.setter
    def set_transmit(self, arg1: collections.abc.Callable[[bool], Error | None] | None) -> None: ...

class SerialOptions:
    def __init__(
        self,
        *,
        baud_rate: int = ...,
        channel: ChannelOptions = ...,
        data_bits: int = ...,
        flow_control: SerialFlowControl = ...,
        parity: SerialParity = ...,
        stop_bits: SerialStopBits = ...,
    ) -> None: ...
    @property
    def baud_rate(self) -> int: ...
    @baud_rate.setter
    def baud_rate(self, arg1: int) -> None: ...
    @property
    def channel(self) -> ChannelOptions: ...
    @channel.setter
    def channel(self, arg1: ChannelOptions) -> None: ...
    @property
    def data_bits(self) -> int: ...
    @data_bits.setter
    def data_bits(self, arg1: int) -> None: ...
    @property
    def flow_control(self) -> SerialFlowControl: ...
    @flow_control.setter
    def flow_control(self, arg1: SerialFlowControl) -> None: ...
    @property
    def parity(self) -> SerialParity: ...
    @parity.setter
    def parity(self, arg1: SerialParity) -> None: ...
    @property
    def stop_bits(self) -> SerialStopBits: ...
    @stop_bits.setter
    def stop_bits(self, arg1: SerialStopBits) -> None: ...

class SerialParity:
    """
    Members:

      none

      odd

      even
    """

    __members__: typing.ClassVar[dict[str, SerialParity]]
    even: typing.ClassVar[SerialParity]
    none: typing.ClassVar[SerialParity]
    odd: typing.ClassVar[SerialParity]

    @typing.overload
    def __eq__(self, other: SerialParity) -> bool: ...
    @typing.overload
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __int__(self) -> int: ...
    @typing.overload
    def __ne__(self, other: SerialParity) -> bool: ...
    @typing.overload
    def __ne__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __str__(self) -> str: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class SerialStopBits:
    """
    Members:

      one

      one_point_five

      two
    """

    __members__: typing.ClassVar[dict[str, SerialStopBits]]
    one: typing.ClassVar[SerialStopBits]
    one_point_five: typing.ClassVar[SerialStopBits]
    two: typing.ClassVar[SerialStopBits]

    @typing.overload
    def __eq__(self, other: SerialStopBits) -> bool: ...
    @typing.overload
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __int__(self) -> int: ...
    @typing.overload
    def __ne__(self, other: SerialStopBits) -> bool: ...
    @typing.overload
    def __ne__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __str__(self) -> str: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class ServerAddress:
    def __init__(
        self, *, bytes: bytes = ..., logical: int = ..., type: AddressType = ...
    ) -> None: ...
    @property
    def bytes(self) -> bytes: ...
    @bytes.setter
    def bytes(self, arg1: bytes) -> None: ...
    @property
    def logical(self) -> int: ...
    @logical.setter
    def logical(self, arg1: int) -> None: ...
    @property
    def type(self) -> AddressType: ...
    @type.setter
    def type(self, arg1: AddressType) -> None: ...

class ServerOperation:
    def __init__(self, *, error: Error | None = ..., token: int = ...) -> None: ...
    @property
    def error(self) -> Error | None: ...
    @error.setter
    def error(self, arg1: Error | None) -> None: ...
    @property
    def token(self) -> int: ...
    @token.setter
    def token(self, arg1: int) -> None: ...

class ServerOptions:
    def __init__(
        self,
        *,
        channel: ChannelOptions = ...,
        heartbeat_seconds: int = ...,
        max_connections: int = ...,
        protocol: SessionOptions = ...,
    ) -> None: ...
    @property
    def channel(self) -> ChannelOptions: ...
    @channel.setter
    def channel(self, arg1: ChannelOptions) -> None: ...
    @property
    def heartbeat_seconds(self) -> int: ...
    @heartbeat_seconds.setter
    def heartbeat_seconds(self, arg1: int) -> None: ...
    @property
    def max_connections(self) -> int: ...
    @max_connections.setter
    def max_connections(self, arg1: int) -> None: ...
    @property
    def protocol(self) -> SessionOptions: ...
    @protocol.setter
    def protocol(self, arg1: SessionOptions) -> None: ...

class ServerRunner:
    def __init__(self, server: NativeServer) -> None: ...
    def poll(self) -> ServerOperation | None: ...
    def start_serial(
        self,
        path: str,
        baud: typing.SupportsInt | typing.SupportsIndex = 9600,
        profile: ConnectionProfile = ConnectionProfile.local_public,
    ) -> int: ...
    def start_serial_configured(
        self,
        path: str,
        serial: SerialOptions,
        link: SerialLinkOptions,
        profile: ConnectionProfile = ConnectionProfile.local_public,
    ) -> int: ...
    def start_tcp(
        self,
        address: str,
        port: typing.SupportsInt | typing.SupportsIndex,
        profile: ConnectionProfile = ConnectionProfile.remote_public,
    ) -> int: ...
    def stop(self) -> int: ...

class ServerState:
    """
    Members:

      stopped

      starting

      running

      stopping
    """

    __members__: typing.ClassVar[dict[str, ServerState]]
    running: typing.ClassVar[ServerState]
    starting: typing.ClassVar[ServerState]
    stopped: typing.ClassVar[ServerState]
    stopping: typing.ClassVar[ServerState]

    @typing.overload
    def __eq__(self, other: ServerState) -> bool: ...
    @typing.overload
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __int__(self) -> int: ...
    @typing.overload
    def __ne__(self, other: ServerState) -> bool: ...
    @typing.overload
    def __ne__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __str__(self) -> str: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class SessionHandle:
    def __init__(
        self, channel: IChannel, executor: IExecutor, options: SessionOptions = ...
    ) -> None: ...
    def async_action(
        self,
        methods: collections.abc.Sequence[ActionMethod],
        list: bool,
        callback: collections.abc.Callable[[ActionResponse | Error], None],
    ) -> None: ...
    def async_connect(
        self, callback: collections.abc.Callable[[ConnectResponse | Error], None]
    ) -> None: ...
    def async_exchange(
        self,
        request: LinkRequest
        | LinkResponse
        | ConnectRequest
        | ConnectResponse
        | ReleaseRequest
        | ReleaseResponse
        | ReleaseNotification
        | ErrorResponse
        | GetRequest
        | GetResponse
        | SetRequest
        | SetResponse
        | ActionRequest
        | ActionResponse
        | GetRecordRequest
        | GetRecordResponse
        | GetNextRequest
        | GetNextResponse
        | GetMd5Request
        | GetMd5Response
        | SetThenGetRequest
        | SetThenGetResponse
        | ActionThenGetRequest
        | ActionThenGetResponse
        | ReportNotification
        | ReportResponse
        | ProxyRequest
        | ProxyResponse,
        callback: collections.abc.Callable[
            [
                LinkRequest
                | LinkResponse
                | ConnectRequest
                | ConnectResponse
                | ReleaseRequest
                | ReleaseResponse
                | ReleaseNotification
                | ErrorResponse
                | GetRequest
                | GetResponse
                | SetRequest
                | SetResponse
                | ActionRequest
                | ActionResponse
                | GetRecordRequest
                | GetRecordResponse
                | GetNextRequest
                | GetNextResponse
                | GetMd5Request
                | GetMd5Response
                | SetThenGetRequest
                | SetThenGetResponse
                | ActionThenGetRequest
                | ActionThenGetResponse
                | ReportNotification
                | ReportResponse
                | ProxyRequest
                | ProxyResponse
                | Error
            ],
            None,
        ],
    ) -> None: ...
    def async_get(
        self,
        attributes: collections.abc.Sequence[Oad],
        list: bool,
        callback: collections.abc.Callable[[GetResponse | Error], None],
    ) -> None: ...
    def async_get_record(
        self,
        records: collections.abc.Sequence[GetRecord],
        list: bool,
        callback: collections.abc.Callable[[GetRecordResponse | Error], None],
    ) -> None: ...
    def async_link(
        self,
        type: LinkRequestType,
        heartbeat_seconds: typing.SupportsInt | typing.SupportsIndex,
        callback: collections.abc.Callable[[LinkResponse | Error], None],
    ) -> None: ...
    def async_release(self, callback: collections.abc.Callable[[None | Error], None]) -> None: ...
    def async_set(
        self,
        attributes: collections.abc.Sequence[SetAttribute],
        list: bool,
        callback: collections.abc.Callable[[SetResponse | Error], None],
    ) -> None: ...
    def cancel(self) -> None: ...
    def in_executor_thread(self) -> bool: ...
    def request_close(self) -> None: ...
    def set_access_demand(self, arg0: bool) -> None: ...
    def set_acd_handler(self, callback: collections.abc.Callable[[], None] | None) -> None: ...
    def set_action_handler(
        self, callback: collections.abc.Callable[[ActionRequest], ActionResponse] | None
    ) -> None: ...
    def set_advanced_handler(
        self,
        callback: collections.abc.Callable[
            [
                LinkRequest
                | LinkResponse
                | ConnectRequest
                | ConnectResponse
                | ReleaseRequest
                | ReleaseResponse
                | ReleaseNotification
                | ErrorResponse
                | GetRequest
                | GetResponse
                | SetRequest
                | SetResponse
                | ActionRequest
                | ActionResponse
                | GetRecordRequest
                | GetRecordResponse
                | GetNextRequest
                | GetNextResponse
                | GetMd5Request
                | GetMd5Response
                | SetThenGetRequest
                | SetThenGetResponse
                | ActionThenGetRequest
                | ActionThenGetResponse
                | ReportNotification
                | ReportResponse
                | ProxyRequest
                | ProxyResponse,
                collections.abc.Callable[
                    [
                        LinkRequest
                        | LinkResponse
                        | ConnectRequest
                        | ConnectResponse
                        | ReleaseRequest
                        | ReleaseResponse
                        | ReleaseNotification
                        | ErrorResponse
                        | GetRequest
                        | GetResponse
                        | SetRequest
                        | SetResponse
                        | ActionRequest
                        | ActionResponse
                        | GetRecordRequest
                        | GetRecordResponse
                        | GetNextRequest
                        | GetNextResponse
                        | GetMd5Request
                        | GetMd5Response
                        | SetThenGetRequest
                        | SetThenGetResponse
                        | ActionThenGetRequest
                        | ActionThenGetResponse
                        | ReportNotification
                        | ReportResponse
                        | ProxyRequest
                        | ProxyResponse
                        | Error
                    ],
                    None,
                ],
            ],
            collections.abc.Callable[[], None] | None,
        ]
        | None,
    ) -> None: ...
    def set_close_handler(
        self, callback: collections.abc.Callable[[Error], None] | None
    ) -> None: ...
    def set_diagnostic_handler(
        self, callback: collections.abc.Callable[[Error], None] | None
    ) -> None: ...
    def set_follow_handler(
        self,
        callback: collections.abc.Callable[
            [builtins.list[AttributeResult] | builtins.list[RecordResult]], None
        ]
        | None,
    ) -> None: ...
    def set_record_handler(
        self, callback: collections.abc.Callable[[GetRecordRequest], GetRecordResponse] | None
    ) -> None: ...
    def set_report_handler(
        self, callback: collections.abc.Callable[[ReportNotification], bool] | None
    ) -> None: ...
    def set_request_handler(
        self, callback: collections.abc.Callable[[GetRequest], GetResponse] | None
    ) -> None: ...
    def set_set_handler(
        self, callback: collections.abc.Callable[[SetRequest], SetResponse] | None
    ) -> None: ...
    def set_state_handler(
        self, callback: collections.abc.Callable[[SessionState], None] | None
    ) -> None: ...
    def set_traffic_handler(
        self, callback: collections.abc.Callable[[Event], None] | None
    ) -> None: ...
    def start(self) -> None: ...
    @property
    def state(self) -> SessionState: ...

class SessionOptions:
    def __init__(
        self,
        *,
        calendar_clock: collections.abc.Callable[[], DateTime] | None = ...,
        client_address: int = ...,
        clock_trusted: bool = ...,
        factory: FactoryVersion = ...,
        fragment_retries: int = ...,
        fragment_timeout: float = ...,
        heartbeat_seconds: int = ...,
        id_reuse_delay: float = ...,
        limits: Limits = ...,
        parameters: AssociationParameters = ...,
        prefer_get_blocks: bool = ...,
        preset_association: bool = ...,
        protect_application: bool = ...,
        reassembly_timeout: float = ...,
        report_retries: int = ...,
        request_time_tag: Ti | None = ...,
        request_timeout: float = ...,
        require_login: bool = ...,
        role: Role = ...,
        security_backend: SecurityBackend | None = ...,
        security_backend_factory: collections.abc.Callable[[], SecurityBackend] | None = ...,
        server: ServerAddress = ...,
    ) -> None: ...
    @property
    def calendar_clock(self) -> collections.abc.Callable[[], DateTime] | None: ...
    @calendar_clock.setter
    def calendar_clock(self, arg1: collections.abc.Callable[[], DateTime] | None) -> None: ...
    @property
    def client_address(self) -> int: ...
    @client_address.setter
    def client_address(self, arg1: int) -> None: ...
    @property
    def clock_trusted(self) -> bool: ...
    @clock_trusted.setter
    def clock_trusted(self, arg1: bool) -> None: ...
    @property
    def factory(self) -> FactoryVersion: ...
    @factory.setter
    def factory(self, arg1: FactoryVersion) -> None: ...
    @property
    def fragment_retries(self) -> int: ...
    @fragment_retries.setter
    def fragment_retries(self, arg1: int) -> None: ...
    @property
    def fragment_timeout(self) -> float: ...
    @fragment_timeout.setter
    def fragment_timeout(self, arg1: float) -> None: ...
    @property
    def heartbeat_seconds(self) -> int: ...
    @heartbeat_seconds.setter
    def heartbeat_seconds(self, arg1: int) -> None: ...
    @property
    def id_reuse_delay(self) -> float: ...
    @id_reuse_delay.setter
    def id_reuse_delay(self, arg1: float) -> None: ...
    @property
    def limits(self) -> Limits: ...
    @limits.setter
    def limits(self, arg1: Limits) -> None: ...
    @property
    def parameters(self) -> AssociationParameters: ...
    @parameters.setter
    def parameters(self, arg1: AssociationParameters) -> None: ...
    @property
    def prefer_get_blocks(self) -> bool: ...
    @prefer_get_blocks.setter
    def prefer_get_blocks(self, arg1: bool) -> None: ...
    @property
    def preset_association(self) -> bool: ...
    @preset_association.setter
    def preset_association(self, arg1: bool) -> None: ...
    @property
    def protect_application(self) -> bool: ...
    @protect_application.setter
    def protect_application(self, arg1: bool) -> None: ...
    @property
    def reassembly_timeout(self) -> float: ...
    @reassembly_timeout.setter
    def reassembly_timeout(self, arg1: float) -> None: ...
    @property
    def report_retries(self) -> int: ...
    @report_retries.setter
    def report_retries(self, arg1: int) -> None: ...
    @property
    def request_time_tag(self) -> Ti | None: ...
    @request_time_tag.setter
    def request_time_tag(self, arg1: Ti | None) -> None: ...
    @property
    def request_timeout(self) -> float: ...
    @request_timeout.setter
    def request_timeout(self, arg1: float) -> None: ...
    @property
    def require_login(self) -> bool: ...
    @require_login.setter
    def require_login(self, arg1: bool) -> None: ...
    @property
    def role(self) -> Role: ...
    @role.setter
    def role(self, arg1: Role) -> None: ...
    @property
    def security_backend(self) -> SecurityBackend | None: ...
    @security_backend.setter
    def security_backend(self, arg1: SecurityBackend | None) -> None: ...
    @property
    def security_backend_factory(self) -> collections.abc.Callable[[], SecurityBackend] | None: ...
    @security_backend_factory.setter
    def security_backend_factory(
        self, arg1: collections.abc.Callable[[], SecurityBackend] | None
    ) -> None: ...
    @property
    def server(self) -> ServerAddress: ...
    @server.setter
    def server(self, arg1: ServerAddress) -> None: ...

class SessionState:
    """
    Members:

      disconnected

      preconnected

      associating

      associated

      releasing

      closed
    """

    __members__: typing.ClassVar[dict[str, SessionState]]
    associated: typing.ClassVar[SessionState]
    associating: typing.ClassVar[SessionState]
    closed: typing.ClassVar[SessionState]
    disconnected: typing.ClassVar[SessionState]
    preconnected: typing.ClassVar[SessionState]
    releasing: typing.ClassVar[SessionState]

    @typing.overload
    def __eq__(self, other: SessionState) -> bool: ...
    @typing.overload
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __int__(self) -> int: ...
    @typing.overload
    def __ne__(self, other: SessionState) -> bool: ...
    @typing.overload
    def __ne__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __str__(self) -> str: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class SetAttribute:
    def __init__(self, *, attribute: Oad = ..., value: Data = ...) -> None: ...
    @property
    def attribute(self) -> Oad: ...
    @attribute.setter
    def attribute(self, arg1: Oad) -> None: ...
    @property
    def value(self) -> Data: ...
    @value.setter
    def value(self, arg1: Data) -> None: ...

class SetRequest:
    def __init__(
        self,
        *,
        attributes: builtins.list[SetAttribute] = ...,
        list: bool = ...,
        piid: int = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def attributes(self) -> builtins.list[SetAttribute]: ...
    @attributes.setter
    def attributes(self, arg1: builtins.list[SetAttribute]) -> None: ...
    @property
    def list(self) -> bool: ...
    @list.setter
    def list(self, arg1: bool) -> None: ...
    @property
    def piid(self) -> int: ...
    @piid.setter
    def piid(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class SetResponse:
    def __init__(
        self,
        *,
        attributes: builtins.list[SetResult] = ...,
        follow_report: builtins.list[AttributeResult] | builtins.list[RecordResult] | None = ...,
        list: bool = ...,
        piid_acd: int = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def attributes(self) -> builtins.list[SetResult]: ...
    @attributes.setter
    def attributes(self, arg1: builtins.list[SetResult]) -> None: ...
    @property
    def follow_report(
        self,
    ) -> builtins.list[AttributeResult] | builtins.list[RecordResult] | None: ...
    @follow_report.setter
    def follow_report(
        self, arg1: builtins.list[AttributeResult] | builtins.list[RecordResult] | None
    ) -> None: ...
    @property
    def list(self) -> bool: ...
    @list.setter
    def list(self, arg1: bool) -> None: ...
    @property
    def piid_acd(self) -> int: ...
    @piid_acd.setter
    def piid_acd(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class SetResult:
    def __init__(self, *, attribute: Oad = ..., dar: int = ...) -> None: ...
    @property
    def attribute(self) -> Oad: ...
    @attribute.setter
    def attribute(self, arg1: Oad) -> None: ...
    @property
    def dar(self) -> int: ...
    @dar.setter
    def dar(self, arg1: int) -> None: ...

class SetThenGet:
    def __init__(
        self, *, delay_seconds: int = ..., read: Oad = ..., set: SetAttribute = ...
    ) -> None: ...
    @property
    def delay_seconds(self) -> int: ...
    @delay_seconds.setter
    def delay_seconds(self, arg1: int) -> None: ...
    @property
    def read(self) -> Oad: ...
    @read.setter
    def read(self, arg1: Oad) -> None: ...
    @property
    def set(self) -> SetAttribute: ...
    @set.setter
    def set(self, arg1: SetAttribute) -> None: ...

class SetThenGetRequest:
    def __init__(
        self,
        *,
        items: builtins.list[SetThenGet] = ...,
        piid: int = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def items(self) -> builtins.list[SetThenGet]: ...
    @items.setter
    def items(self, arg1: builtins.list[SetThenGet]) -> None: ...
    @property
    def piid(self) -> int: ...
    @piid.setter
    def piid(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class SetThenGetResponse:
    def __init__(
        self,
        *,
        follow_report: builtins.list[AttributeResult] | builtins.list[RecordResult] | None = ...,
        items: builtins.list[SetThenGetResult] = ...,
        piid_acd: int = ...,
        time_tag: TimeTag | None = ...,
    ) -> None: ...
    @property
    def follow_report(
        self,
    ) -> builtins.list[AttributeResult] | builtins.list[RecordResult] | None: ...
    @follow_report.setter
    def follow_report(
        self, arg1: builtins.list[AttributeResult] | builtins.list[RecordResult] | None
    ) -> None: ...
    @property
    def items(self) -> builtins.list[SetThenGetResult]: ...
    @items.setter
    def items(self, arg1: builtins.list[SetThenGetResult]) -> None: ...
    @property
    def piid_acd(self) -> int: ...
    @piid_acd.setter
    def piid_acd(self, arg1: int) -> None: ...
    @property
    def time_tag(self) -> TimeTag | None: ...
    @time_tag.setter
    def time_tag(self, arg1: TimeTag | None) -> None: ...

class SetThenGetResult:
    def __init__(self, *, read: AttributeResult = ..., set: SetResult = ...) -> None: ...
    @property
    def read(self) -> AttributeResult: ...
    @read.setter
    def read(self, arg1: AttributeResult) -> None: ...
    @property
    def set(self) -> SetResult: ...
    @set.setter
    def set(self, arg1: SetResult) -> None: ...

class Sid:
    def __init__(self, *, additional: bytes = ..., identifier: int = ...) -> None: ...
    @property
    def additional(self) -> bytes: ...
    @additional.setter
    def additional(self, arg1: bytes) -> None: ...
    @property
    def identifier(self) -> int: ...
    @identifier.setter
    def identifier(self, arg1: int) -> None: ...

class SidMac:
    def __init__(self, *, mac: Mac = ..., sid: Sid = ...) -> None: ...
    @property
    def mac(self) -> Mac: ...
    @mac.setter
    def mac(self, arg1: Mac) -> None: ...
    @property
    def sid(self) -> Sid: ...
    @sid.setter
    def sid(self, arg1: Sid) -> None: ...

class SignatureSecurity:
    def __init__(self, *, ciphertext: bytes = ..., signature: bytes = ...) -> None: ...
    @property
    def ciphertext(self) -> bytes: ...
    @ciphertext.setter
    def ciphertext(self, arg1: bytes) -> None: ...
    @property
    def signature(self) -> bytes: ...
    @signature.setter
    def signature(self, arg1: bytes) -> None: ...

class Support:
    """
    Members:

      unknown

      no

      yes
    """

    __members__: typing.ClassVar[dict[str, Support]]
    no: typing.ClassVar[Support]
    unknown: typing.ClassVar[Support]
    yes: typing.ClassVar[Support]

    @typing.overload
    def __eq__(self, other: Support) -> bool: ...
    @typing.overload
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __int__(self) -> int: ...
    @typing.overload
    def __ne__(self, other: Support) -> bool: ...
    @typing.overload
    def __ne__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __str__(self) -> str: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

class SymmetrySecurity:
    def __init__(self, *, ciphertext: bytes = ..., signature: bytes = ...) -> None: ...
    @property
    def ciphertext(self) -> bytes: ...
    @ciphertext.setter
    def ciphertext(self, arg1: bytes) -> None: ...
    @property
    def signature(self) -> bytes: ...
    @signature.setter
    def signature(self, arg1: bytes) -> None: ...

class TcpChannel(IChannel):
    @staticmethod
    def connect(
        runtime: IoRuntime,
        host: str,
        port: typing.SupportsInt | typing.SupportsIndex,
        callback: collections.abc.Callable[[None | Error], None],
        options: ChannelOptions = ...,
    ) -> TcpChannel: ...

class TcpListener:
    @staticmethod
    def listen(
        runtime: IoRuntime,
        address: str,
        port: typing.SupportsInt | typing.SupportsIndex,
        options: ChannelOptions = ...,
    ) -> TcpListener: ...
    def async_accept(
        self, callback: collections.abc.Callable[[TcpChannel | Error], None]
    ) -> None: ...
    def close(self) -> None: ...
    @property
    def local_port(self) -> int: ...

class Ti:
    def __init__(self, *, interval: int = ..., unit: int = ...) -> None: ...
    @property
    def interval(self) -> int: ...
    @interval.setter
    def interval(self, arg1: int) -> None: ...
    @property
    def unit(self) -> int: ...
    @unit.setter
    def unit(self, arg1: int) -> None: ...

class Time:
    def __init__(
        self, *, value: typing.Annotated[builtins.list[int], "FixedSize(3)"] = ...
    ) -> None: ...
    @property
    def value(self) -> typing.Annotated[builtins.list[int], "FixedSize(3)"]: ...
    @value.setter
    def value(self, arg1: typing.Annotated[builtins.list[int], "FixedSize(3)"]) -> None: ...

class TimeTag:
    def __init__(self, *, allowed_delay: Ti = ..., sent_at: DateTimeS = ...) -> None: ...
    @property
    def allowed_delay(self) -> Ti: ...
    @allowed_delay.setter
    def allowed_delay(self, arg1: Ti) -> None: ...
    @property
    def sent_at(self) -> DateTimeS: ...
    @sent_at.setter
    def sent_at(self, arg1: DateTimeS) -> None: ...

class TransBridge:
    def __init__(
        self,
        max_pending: typing.SupportsInt | typing.SupportsIndex = 128,
        max_bytes: typing.SupportsInt | typing.SupportsIndex = 1048576,
    ) -> None: ...
    def close(self) -> None: ...
    def complete(
        self, token: typing.SupportsInt | typing.SupportsIndex, result: ProxyTransResponse | Error
    ) -> bool: ...
    def drain(self) -> builtins.list[TransJob]: ...

class TransData:
    def __init__(self, *, data: builtins.list[bytes] = ..., port: Oad = ...) -> None: ...
    @property
    def data(self) -> builtins.list[bytes]: ...
    @data.setter
    def data(self, arg1: builtins.list[bytes]) -> None: ...
    @property
    def port(self) -> Oad: ...
    @port.setter
    def port(self, arg1: Oad) -> None: ...

class TransJob:
    @property
    def request(self) -> ProxyTransRequest: ...
    @property
    def token(self) -> int: ...

class Tsa:
    def __init__(self, *, value: bytes = ...) -> None: ...
    @property
    def value(self) -> bytes: ...
    @value.setter
    def value(self, arg1: bytes) -> None: ...

class UInt16:
    def __init__(self, *, value: int = ...) -> None: ...
    @property
    def value(self) -> int: ...
    @value.setter
    def value(self, arg1: int) -> None: ...

class UInt32:
    def __init__(self, *, value: int = ...) -> None: ...
    @property
    def value(self) -> int: ...
    @value.setter
    def value(self, arg1: int) -> None: ...

class UInt64:
    def __init__(self, *, value: int = ...) -> None: ...
    @property
    def value(self) -> int: ...
    @value.setter
    def value(self, arg1: int) -> None: ...

class UInt8:
    def __init__(self, *, value: int = ...) -> None: ...
    @property
    def value(self) -> int: ...
    @value.setter
    def value(self, arg1: int) -> None: ...

class Utf8String:
    def __init__(self, *, value: str = ...) -> None: ...
    @property
    def value(self) -> str: ...
    @value.setter
    def value(self, arg1: str) -> None: ...

class ValueDefinition:
    @property
    def allowed_values(self) -> builtins.list[int]: ...
    @property
    def fields(self) -> builtins.list[ValueDefinition]: ...
    @property
    def maximum(self) -> int | None: ...
    @property
    def name(self) -> str: ...
    @property
    def scaling(self) -> ScalerUnit | None: ...
    @property
    def size(self) -> int | None: ...
    @property
    def type(self) -> DataType: ...

class VisibleString:
    def __init__(self, *, value: str = ...) -> None: ...
    @property
    def value(self) -> str: ...
    @value.setter
    def value(self, arg1: str) -> None: ...

class Wiring:
    """
    Members:

      single_phase

      three_phase
    """

    __members__: typing.ClassVar[dict[str, Wiring]]
    single_phase: typing.ClassVar[Wiring]
    three_phase: typing.ClassVar[Wiring]

    @typing.overload
    def __eq__(self, other: Wiring) -> bool: ...
    @typing.overload
    def __eq__(self, other: object) -> bool: ...
    def __getstate__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __index__(self) -> int: ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __int__(self) -> int: ...
    @typing.overload
    def __ne__(self, other: Wiring) -> bool: ...
    @typing.overload
    def __ne__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None: ...
    def __str__(self) -> str: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...

def advanced_capability(
    arg0: LinkRequest
    | LinkResponse
    | ConnectRequest
    | ConnectResponse
    | ReleaseRequest
    | ReleaseResponse
    | ReleaseNotification
    | ErrorResponse
    | GetRequest
    | GetResponse
    | SetRequest
    | SetResponse
    | ActionRequest
    | ActionResponse
    | GetRecordRequest
    | GetRecordResponse
    | GetNextRequest
    | GetNextResponse
    | GetMd5Request
    | GetMd5Response
    | SetThenGetRequest
    | SetThenGetResponse
    | ActionThenGetRequest
    | ActionThenGetResponse
    | ReportNotification
    | ReportResponse
    | ProxyRequest
    | ProxyResponse,
) -> int: ...
def advanced_matches(
    arg0: LinkRequest
    | LinkResponse
    | ConnectRequest
    | ConnectResponse
    | ReleaseRequest
    | ReleaseResponse
    | ReleaseNotification
    | ErrorResponse
    | GetRequest
    | GetResponse
    | SetRequest
    | SetResponse
    | ActionRequest
    | ActionResponse
    | GetRecordRequest
    | GetRecordResponse
    | GetNextRequest
    | GetNextResponse
    | GetMd5Request
    | GetMd5Response
    | SetThenGetRequest
    | SetThenGetResponse
    | ActionThenGetRequest
    | ActionThenGetResponse
    | ReportNotification
    | ReportResponse
    | ProxyRequest
    | ProxyResponse,
    arg1: LinkRequest
    | LinkResponse
    | ConnectRequest
    | ConnectResponse
    | ReleaseRequest
    | ReleaseResponse
    | ReleaseNotification
    | ErrorResponse
    | GetRequest
    | GetResponse
    | SetRequest
    | SetResponse
    | ActionRequest
    | ActionResponse
    | GetRecordRequest
    | GetRecordResponse
    | GetNextRequest
    | GetNextResponse
    | GetMd5Request
    | GetMd5Response
    | SetThenGetRequest
    | SetThenGetResponse
    | ActionThenGetRequest
    | ActionThenGetResponse
    | ReportNotification
    | ReportResponse
    | ProxyRequest
    | ProxyResponse,
) -> bool: ...
def approximate_value(arg0: ScaledNumber) -> float: ...
def async_probe_points(
    session: SessionHandle,
    capabilities: Capabilities,
    attributes: collections.abc.Sequence[Oad],
    options: ProbeOptions,
    callback: collections.abc.Callable[[builtins.list[PointResult] | Error], None],
) -> None: ...
def attach_advanced_services(
    session: SessionHandle,
    objects: ObjectRegistry,
    executor: IExecutor,
    options: AdvancedServiceOptions = ...,
    transparent: TransBridge | None = None,
) -> None: ...
def attach_services(session: SessionHandle, objects: ObjectRegistry) -> None: ...
def build_info() -> dict[str, str]: ...
def candidate_points(
    capabilities: Capabilities,
    attributes: collections.abc.Sequence[Oad],
    discard_negative: bool = False,
) -> builtins.list[CandidatePoint]: ...
def capabilities_from_connect(response: ConnectResponse) -> Capabilities: ...
def crc16(arg0: bytes | bytearray | memoryview) -> int: ...
def decimal_text(arg0: ScaledNumber) -> str: ...
def decode_apdu(
    input: bytes | bytearray | memoryview, limits: Limits = ...
) -> (
    LinkRequest
    | LinkResponse
    | ConnectRequest
    | ConnectResponse
    | ReleaseRequest
    | ReleaseResponse
    | ReleaseNotification
    | ErrorResponse
    | GetRequest
    | GetResponse
    | SetRequest
    | SetResponse
    | ActionRequest
    | ActionResponse
    | GetRecordRequest
    | GetRecordResponse
    | GetNextRequest
    | GetNextResponse
    | GetMd5Request
    | GetMd5Response
    | SetThenGetRequest
    | SetThenGetResponse
    | ActionThenGetRequest
    | ActionThenGetResponse
    | ReportNotification
    | ReportResponse
    | ProxyRequest
    | ProxyResponse
): ...
def decode_data(input: bytes | bytearray | memoryview, limits: Limits = ...) -> Data: ...
def decode_fragment(payload: bytes | bytearray | memoryview) -> Fragment: ...
def decode_frame(input: bytes | bytearray | memoryview, limits: Limits = ...) -> Frame: ...
def decode_security(
    input: bytes | bytearray | memoryview, limits: Limits = ...
) -> SecurityRequest | SecurityResponse: ...
def demand_oad(
    family: typing.SupportsInt | typing.SupportsIndex,
    phase: Phase,
    tariff: typing.SupportsInt | typing.SupportsIndex,
    layout: DeviceLayout = ...,
) -> Oad: ...
def demand_values(
    attribute: Oad, value: Data, layout: DeviceLayout = ..., limits: Limits = ...
) -> builtins.list[DemandValue]: ...
def encode_apdu(
    value: LinkRequest
    | LinkResponse
    | ConnectRequest
    | ConnectResponse
    | ReleaseRequest
    | ReleaseResponse
    | ReleaseNotification
    | ErrorResponse
    | GetRequest
    | GetResponse
    | SetRequest
    | SetResponse
    | ActionRequest
    | ActionResponse
    | GetRecordRequest
    | GetRecordResponse
    | GetNextRequest
    | GetNextResponse
    | GetMd5Request
    | GetMd5Response
    | SetThenGetRequest
    | SetThenGetResponse
    | ActionThenGetRequest
    | ActionThenGetResponse
    | ReportNotification
    | ReportResponse
    | ProxyRequest
    | ProxyResponse,
    limits: Limits = ...,
) -> bytes: ...
def encode_data(value: Data, limits: Limits = ...) -> bytes: ...
def encode_fragment(fragment: Fragment) -> bytes: ...
def encode_frame(value: Frame, limits: Limits = ...) -> bytes: ...
def encode_security(value: SecurityRequest | SecurityResponse, limits: Limits = ...) -> bytes: ...
def energy_oad(
    family: typing.SupportsInt | typing.SupportsIndex,
    phase: Phase,
    tariff: typing.SupportsInt | typing.SupportsIndex,
    layout: DeviceLayout = ...,
    high_precision: bool = False,
) -> Oad: ...
def engineering_values(
    attribute: Oad, value: Data, layout: DeviceLayout = ..., limits: Limits = ...
) -> builtins.list[ScaledNumber]: ...
def find_attribute(arg0: Oad) -> AttributeDefinition | None: ...
def find_object(arg0: typing.SupportsInt | typing.SupportsIndex) -> ObjectDefinition | None: ...
def find_record(arg0: typing.SupportsInt | typing.SupportsIndex) -> RecordDefinition | None: ...
def harmonic_oad(
    oi: typing.SupportsInt | typing.SupportsIndex,
    phase: Phase,
    order: typing.SupportsInt | typing.SupportsIndex,
    layout: DeviceLayout = ...,
) -> Oad: ...
def make_oad(
    oi: typing.SupportsInt | typing.SupportsIndex,
    attribute: typing.SupportsInt | typing.SupportsIndex = 2,
    index: typing.SupportsInt | typing.SupportsIndex = 0,
    layout: DeviceLayout = ...,
) -> Oad: ...
def make_object_schema(
    arg0: typing.SupportsInt | typing.SupportsIndex, arg1: bytes
) -> ObjectSchema: ...
def make_record_query(
    oi: typing.SupportsInt | typing.SupportsIndex,
    rows: SelectAll
    | Selector1
    | Selector2
    | Selector3
    | Selector4
    | Selector5
    | Selector6
    | Selector7
    | Selector8
    | Selector9
    | Selector10,
    columns: collections.abc.Sequence[Oad] = [],
    layout: DeviceLayout = ...,
    limits: Limits = ...,
) -> GetRecord: ...
def objects() -> builtins.list[ObjectDefinition]: ...
def phase_oad(
    oi: typing.SupportsInt | typing.SupportsIndex, phase: Phase, layout: DeviceLayout = ...
) -> Oad: ...
def plan_reads(
    capabilities: Capabilities,
    attributes: collections.abc.Sequence[Oad],
    batch_size: typing.SupportsInt | typing.SupportsIndex = 16,
    limits: Limits = ...,
) -> builtins.list[ReadBatch]: ...
def point_support(capabilities: Capabilities, attribute: Oad) -> Support: ...
def record_at(
    oi: typing.SupportsInt | typing.SupportsIndex,
    time: DateTimeS,
    columns: collections.abc.Sequence[Oad] = [],
    layout: DeviceLayout = ...,
    limits: Limits = ...,
) -> GetRecord: ...
def record_between(
    oi: typing.SupportsInt | typing.SupportsIndex,
    begin: DateTimeS,
    end: DateTimeS,
    columns: collections.abc.Sequence[Oad] = [],
    layout: DeviceLayout = ...,
    limits: Limits = ...,
) -> GetRecord: ...
def record_sequences(
    oi: typing.SupportsInt | typing.SupportsIndex,
    begin: typing.SupportsInt | typing.SupportsIndex,
    end: typing.SupportsInt | typing.SupportsIndex,
    columns: collections.abc.Sequence[Oad] = [],
    layout: DeviceLayout = ...,
    limits: Limits = ...,
) -> GetRecord: ...
def register_standard_object(
    registry: ObjectRegistry,
    oi: typing.SupportsInt | typing.SupportsIndex,
    attributes: bytes,
    provider: ObjectProvider,
    layout: DeviceLayout = ...,
    limits: Limits = ...,
) -> None: ...
def require_record_service(capabilities: Capabilities) -> None: ...
def service_support(capabilities: Capabilities, service: ReadService) -> Support: ...
def tariff_oad(
    oi: typing.SupportsInt | typing.SupportsIndex,
    tariff: typing.SupportsInt | typing.SupportsIndex,
    layout: DeviceLayout = ...,
    high_precision: bool = False,
) -> Oad: ...
def unit_symbol(arg0: typing.SupportsInt | typing.SupportsIndex) -> str: ...
def validate_layout(arg0: DeviceLayout) -> None: ...
def validate_oad(attribute: Oad, layout: DeviceLayout = ...) -> None: ...
def validate_record_cell(
    oi: typing.SupportsInt | typing.SupportsIndex,
    column: Oad,
    value: Data,
    layout: DeviceLayout = ...,
    limits: Limits = ...,
) -> None: ...
def validate_record_query(
    query: GetRecord, layout: DeviceLayout = ..., limits: Limits = ...
) -> None: ...
def validate_record_result(
    query: GetRecord, result: RecordResult, layout: DeviceLayout = ..., limits: Limits = ...
) -> None: ...
def validate_record_time(time: DateTimeS) -> None: ...
def validate_session_options(options: SessionOptions) -> None: ...
def validate_value(
    attribute: Oad, value: Data, layout: DeviceLayout = ..., limits: Limits = ...
) -> None: ...

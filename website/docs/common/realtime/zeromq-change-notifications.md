# ZeroMQ 变更通知设计（已并入消息与命令设计）

本页内容已并入 [ZeroMQ 消息订阅与命令下发设计](zeromq-messaging-design.md)。新设计明确：实时值使用共享内存或 Redis；变更订阅使用 ZeroMQ PUB/SUB；命令下发使用独立 ZeroMQ REQ/REP 和 MySQL 持久交接。

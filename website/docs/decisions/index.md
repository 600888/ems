# 技术决策与评估

这里保存技术选型的依据、适用条件和重新评估触发条件。具体接口以架构和公共契约为准，评估文档不单独改变运行语义。

- [ZeroMQ 消息订阅与命令下发设计](../common/realtime/zeromq-messaging-design.md)：变更提示、命令交接及可靠性边界。
- [Web 后端采用 Drogon](drogon-web.md)：C++17 Web 技术栈、公共 MySQL Repository 和实施门槛。
- [ZeroMQ 引入评估](zeromq-assessment.md)：2026-09-13 历史评估与可靠性边界。

实时后端决策见 [公共实时访问设计](../common/realtime/access-design.md)；工业协议角色见 [设备、通道与协议](../architecture/channel-protocol.md)。

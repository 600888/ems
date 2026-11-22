# 告警前端接口约定

告警中心位于 `/alarms`，历史查询位于 `/alarms?view=history`。页面与设备监视共用前台壳、设备范围树、指标卡、主题令牌与通用交互组件；状态由 Pinia 管理，API 调用集中在 `src/api/alarm.ts`。恢复与确认是独立事实，前端不会因确认操作而移除活动告警。

告警接口由 `ems-web` 提供。字段类型见 `src/types/alarm.ts`。所有接口使用项目统一的 `{ data, meta }` 响应、同源 Cookie 和 `X-CSRF-Token`，业务参数放在 POST JSON 中。具有整站读取范围的会话可在 `meta.alarm_archive` 获取已提交、已归档位置及待归档事件数。

后端持久化模型、事务与验收矩阵见[告警](../../website/docs/processes/web/alarms.md)。

存储分工：时序数据库保存告警生命周期与历史状态，承担历史查询、详情和历史导出；关系库保存当前状态、首次确认、审计和可靠归档队列。跨库采用事务 outbox 补写；历史读取合并尚未归档的变更，时序库不可用时不回退为关系库历史结果。

| 接口                           | 请求                                                             | 响应 data                                      |
| ------------------------------ | ---------------------------------------------------------------- | ---------------------------------------------- |
| `POST /api/alarms/query`       | `AlarmFilters`、`include_descendants: true`、`page`、`page_size` | `AlarmPage`                                    |
| `POST /api/alarms/detail`      | `alarm_id`                                                       | `AlarmDetail`                                  |
| `POST /api/alarms/acknowledge` | `alarm_ids`、`remark`                                            | `AlarmAckResult`                               |
| `POST /api/alarms/export`      | `AlarmFilters`、`include_descendants: true`、`max_rows: 10000`   | `{ items: AlarmRecord[], truncated: boolean }` |

设备目录直接复用现有 `deviceApi.tree()`，不另建告警设备模型。空 `device_root_id` 表示当前会话的全部授权设备；非空时查询所选节点及下级设备。用于导航的未授权祖先不能扩大读取范围。服务端必须对目录、列表、统计、详情、导出和确认统一执行设备授权，不信任客户端筛选值。

`view` 为 `active` 或 `history`；`severity` 为 `critical`、`warning`、`info` 或空字符串；`acknowledgement` 为 `pending`、`acknowledged` 或空字符串；`recovery` 为 `active`、`cleared` 或空字符串。空字符串表示不按该字段过滤。关键词匹配告警名称、编码及设备。历史查询按发生时间的闭区间 `start_ms` / `end_ms` 筛选，最长 31 天。所有时间戳为 Unix 毫秒，界面和 CSV 统一显示 UTC+8。

列表按 `occurred_at_ms` 降序、`alarm_id` 稳定次序分页，`page_size` 为 8、20 或 50。`total` 和 `summary` 统计完整筛选结果，不是当前页；`unacknowledged` 包含未确认的已恢复告警。`device_counts` 单独统计当前授权范围中每个导航子树的活动告警数，不受关键词等列表筛选影响。未获得结果时界面显示 `—`，不把服务不可用显示成零告警。

`cleared_at_ms: null` 表示尚未恢复；`acknowledgement: null` 表示尚未确认。详情必须返回完整生命周期 `events`。规则条件和关联测点可为空，不具备质量有效的最新值时显示 `—`。`point_id` 可关联现有历史页面，默认定位到发生时间前后 30 分钟。

确认要求 `alarm.ack`，单次最多 200 条，`remark` 去除首尾空白后为 1–500 个字符。服务端逐条校验授权并原子登记首次确认人、时间和备注；与其他值班员并发确认时返回 `already_acknowledged`，不得覆盖首次确认记录。确认写入与审计需要在服务端持久事务内完成，不改变恢复状态。

批量响应为每个请求 ID 返回且仅返回一条结果：`acknowledged`、`already_acknowledged` 或 `failed`，失败可附 `message`。前端在回执成功后重新读取列表和详情，不乐观伪造状态。网络超时、未知 effect 或不完整回执要求先读取详情核对结果，再允许重试，填写的备注会保留。服务端应保证同一告警重复确认不会产生重复业务效果。导出须独立执行授权并返回截断标记，CSV 对文本进行引号转义和公式注入防护。

实时页每 5 秒尝试刷新，可见页面才轮询。选中记录、详情打开或批量对话框打开时暂停列表重排；暂停超过 15 秒或连接中断后，确认前必须刷新。刷新失败保留同一查询的上次结果并显示时效警告；变更筛选、翻页、退出页面和会话/授权变化会隔离过时异步响应。只读角色不显示确认入口。

验证：`npm run build`；`npm test` 中的 `alarm-api.test.mjs` 覆盖生命周期、范围与时间校验、权限、并发查询、部分失败、确认结果未知、会话切换和导出隔离。测试使用内存 HTTP 响应，不依赖告警后端。

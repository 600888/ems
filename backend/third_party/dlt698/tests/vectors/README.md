# 测试向量来源

基线为 `plan/dlt69845-2017.pdf`。`.hex` 文件仅包含独立预期字节，不由待测 C++ 编码器生成。

| 文件 | 来源 | 人工核对的预期值 |
| --- | --- | --- |
| link-login-request.hex | 附录 D.1.1，第 123 页（PDF 126） | PIID-ACD=0，登录，周期 180s，请求时间 2016-05-19 08:05:00.164 |
| link-login-response.hex | 同上 | PIID=0，结果 80H（可信、成功），请求/收到/响应时间保留原示例字段 |
| connect-request.hex | 附录 D.2，第 124 页（PDF 127） | 版本 0010H，8/16 字节全 FF 位图，发送/接收/APDU 1024B，窗口 1，100s，NullSecurity |
| connect-response.hex | 附录 D.2，第 124–125 页（PDF 127–128） | 厂商 TOPS，软件/硬件 0102、日期 160731、8 字节零扩展；允许连接，无认证附加信息/跟随上报/时间标签 |
| release-request.hex | 按 6.3.7.1、6.3.4 构造，第 43、39–40 页 | PIID=1，无 TimeTag |
| release-response.hex | 按 6.3.7.2、6.3.4 构造，第 43、39–41 页 | PIID-ACD=1，成功，无 FollowReport/TimeTag |
| release-notification.hex | 按 6.3.7.3、6.3.4 构造，第 44、39–41 页 | PIID-ACD=2，建立时间 2016-05-19 08:05:00、当前时间 08:06:00，无 FollowReport/TimeTag |
| error-response.hex | 按 6.3.16、6.3.4 构造，第 63、39–41 页 | 服务器 EEH，PIID=3，服务不支持（2），无 FollowReport/TimeTag |
| get-normal-request.hex | 附录 D.3.1，第 125 页（PDF 128） | PIID=1，OAD=40010200，TimeTag 不存在 |
| get-normal-response.hex | 同上 | 相同 OAD，Data=octet-string，6 字节地址，FollowReport/TimeTag 不存在 |
| get-list-request.hex | 附录 D.3.2，第 125 页 | PIID=2，OAD=20000200、20010200 |
| get-list-response.hex | 附录 D.3.2，第 125–126 页 | 电压 array/UInt16: 2413×3；电流 array/Int32: 1000×3 |
| set-normal-request.hex | 附录 D.4.1，第 131 页（PDF 134） | PIID=2，OAD=40000200，date_time_s=2016-01-20 16:27:11，无 TimeTag |
| set-normal-response.hex | 附录 D.4.1，第 131–132 页 | 相同 OAD，DAR=0，无 FollowReport/TimeTag |
| set-list-request.hex | 附录 D.4.2，第 132 页（PDF 135） | PIID=3，通信地址 octet-string=000000000001、同上时钟 |
| set-list-response.hex | 同上 | 两个 OAD 原顺序，DAR 均为 0 |
| action-normal-request.hex | 附录 D.5.1，第 133 页（PDF 136） | PIID=5，OMD=00100100，参数 integer=0，无 TimeTag |
| action-normal-response.hex | 同上 | DAR=0，Data OPTIONAL=0，无 FollowReport/TimeTag |

`cpp/tests/protocol/link/frame_test.cpp` 中另有按第 5 章组成的 GET 帧固定向量，其 APDU 使用 D.3.1、地址采用 D.1；CRC 按附录 A 的初值 FFFF、反射多项式 8408、输出异或 FFFF 独立计算。CRC 的 `123456789 → 906E` 用于验证算法。畸形输入向量明确检查错误类型，不只做往返测试。

上述连接向量已与规范字段及原始页面核对。D.1.1 响应中的请求时间为 .137，与示例请求的 .164 不一致；codec 测试保留两段原字节分别验证布局，不将它们当作有效的会话匹配对。Session 按 6.1.2.3 回显并检查请求时间。RELEASE/ERROR 向量由规范字段手工构造，不声称来自附录示例或设备抓包。

`cpp/tests/protocol/apdu/connection_test.cpp` 对每个向量验证独立模型的精确编码、解码字段、统一 APDU 路由、逐字节截断及尾随字节错误；另以固定预期值覆盖认证 CHOICE、OPTIONAL、TimeTag、非法枚举/保留位和资源限制。`cpp/tests/session/session_test.cpp` 使用虚拟时间与内存通道验证状态和迟到响应行为，不能替代真实设备互操作。

后续扩展每个向量应记录标准版本、章节、原始值、字段预期、错误位置和复核状态。真实设备抓包另存，不能与规范向量混为一致性证据。

`cpp/tests/protocol/apdu/mutation_test.cpp` 将上述六份独立预期字节与明确模型比较，验证逐字节截断、尾随字节、非法 PIID/OPTIONAL、列表预算及 then-get 拒绝。ACTION 列表的固定预期值由 6.3.10.1.3、6.3.10.2.3 手工构造，覆盖存在 NULL 返回与没有返回，不伪称附录 D.5.2 提供了完整 hex。状态测试另验证部分成功、权限/类型/provider 异常、OMD 模式匹配、同步重入和超时。

M5 新增记录向量（已按原始 PDF 页面复核）：

| 文件 | 来源 | 预期 |
| --- | --- | --- |
| get_record_request.hex | D.3.3(1)，印刷 126 页 / PDF 129，按逐字段说明及表 24/67 构造 | PIID=3，日冻结 OAD=50040200，Selector1 冻结时间，RCSD 两列 |
| get_record_response.hex | D.3.3(1)，126–127 页 / PDF 129–130 | 一行两列：date_time_s 和 array/UInt32（总及费率共五个零值） |
| get_record_meters_request.hex | D.3.3(2)，127 页 / PDF 130 | Selector5、MS 三类分支中的 TSA 集合（五表计）、五列含 ROAD 及两个关联 OAD |

D.3.3(1) 的发送整行原文打印为 `...07 E0 16 01 14...`，与下方时间字段 `1C 07 E0 01 14 00 00 00` 不一致；请求 fixture 明确采用逐字段定义，未把错误整行当作合法编码。响应中的年份按十六进制 E0 保存。`cpp/tests/codec/record_codec_test.cpp` 对记录列类型、数组数量、MS/ROAD 字段逐项检查，另有逐字节截断与独立规范字段向量；全部 RSD/MS 分支的往返是补充证据。

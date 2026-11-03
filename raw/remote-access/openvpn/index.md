# OpenVPN 部署说明

本文采用 **OpenVPN 社区版、UDP 1194、TUN 三层网络和证书认证**。
服务端以 Ubuntu 24.04 LTS / OpenVPN 2.6 为基准；客户端覆盖 Linux OpenVPN 2.6 与 Windows OpenVPN GUI。
其他发行版先确认软件版本、路径与 systemd 服务名称，不直接套用旧系统配置。

| 顺序 | 文档 | 完成后得到什么 |
| --- | --- | --- |
| 1 | [服务端部署](server.md) | 可开机启动的 VPN 服务、证书签发环境 |
| 2 | [客户端部署](client.md) | 每台设备独立的证书、Linux `.conf` / Windows `.ovpn` 配置及互通验证 |

需要用脚本统一生成配置并分发 ZIP 时，直接阅读 [脚本生成客户端配置与部署](client-package.md)，
包含完整脚本、参数、包内文件约定，以及实际客户机的安装和启动步骤。

## 网络约定

| 项目 | 本文示例 | 需要修改吗 |
| --- | --- | --- |
| 服务端公网入口 | `121.40.72.224` | 已固定写入客户端配置 |
| 监听协议与端口 | `UDP 1194` | 可调整，服务端、客户端和防火墙必须一致 |
| VPN 网段 | `10.8.0.0/24` | 与任一端现有网段冲突时更换 |
| 服务端虚拟 IP | `10.8.0.1` | 随 VPN 网段变化 |
| 客户端身份 | `client01`、`client02` | 每台设备使用独立名称与证书 |
| 客户端虚拟 IP | `10.8.0.10`、`10.8.0.11` | 由服务端按证书名称固定分配 |

```text
Linux 客户端 client01          公网 OpenVPN 服务端          Windows 客户端 client02
     10.8.0.10  ── UDP 1194 ──  121.40.72.224  ── UDP 1194 ──  10.8.0.11
                                  10.8.0.1
```

目标是让**安装了 VPN 的主机通过虚拟 IP 互访**。客户端主动连接公网服务器，现场侧无需开放入站端口。
本例开启客户端互访，适用于相互信任的运维设备；不会自动接通其背后的整个现场 LAN，也不改变默认上网出口。
接入 PCS、BMS 等现场网段还需要单独规划路由、回程与访问控制，不能仅凭 VPN 已连接判断可达。

## 参考资料

安装与服务管理参考 [Ubuntu 官方指南](https://ubuntu.com/server/docs/how-to/security/install-openvpn/)；
配置参数参考 [OpenVPN 2.6 手册](https://openvpn.net/community-docs/community-articles/openvpn-2-6-manual/)。

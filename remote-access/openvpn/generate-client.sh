#!/bin/sh
# 在 OpenVPN 服务端签发或复用客户端证书，并导出 Windows / Linux ZIP 部署包。
set -eu
umask 077

usage() {
    cat <<'EOF'
用法：/etc/openvpn/scripts/generate-client.sh (--windows | --linux) --name 设备名 [选项]

必选参数：
  --windows          生成 client.ovpn
  --linux            生成 client.conf + 一键安装脚本 install.sh
  --name NAME        设备证书 CN，也是 ZIP 包名的前缀；不改变包内文件名

固定服务端：121.40.72.224（直接写入生成的客户端配置）
控制通道：从 /etc/openvpn/server/server.conf 读取 proto、tls-auth / tls-crypt 和 auth

可选参数：
  --port PORT        服务端口，默认 1194
  --force            覆盖同名 ZIP，不重新签发已有证书
  --help             显示帮助

示例：
  /etc/openvpn/scripts/generate-client.sh --linux --name client01
  /etc/openvpn/scripts/generate-client.sh --windows --name client02

默认目录：
  证书管理：/etc/openvpn/easy-rsa
  导出 ZIP：/etc/openvpn/clients
EOF
}

fail() {
    printf '错误：%s\n' "$*" >&2
    exit 1
}

platform=''
client_name=''
server_port='1194'
force=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --windows|--linux)
            [ -z "$platform" ] || fail '--windows 和 --linux 必须且只能选择一个'
            platform=${1#--}
            shift
            ;;
        --name|--port)
            [ "$#" -ge 2 ] || fail "$1 缺少参数值"
            case "$1" in
                --name) client_name=$2 ;;
                --port) server_port=$2 ;;
            esac
            shift 2
            ;;
        --force) force=1; shift ;;
        --help|-h) usage; exit 0 ;;
        *) fail "未知参数：$1；使用 --help 查看帮助" ;;
    esac
done

[ -n "$platform" ] || fail '请指定 --windows 或 --linux'
case "$client_name" in
    ''|*[!a-zA-Z0-9_-]*) fail '--name 只能包含字母、数字、下划线和连字符' ;;
esac
[ "${#client_name}" -le 64 ] || fail '--name 最长 64 个字符'
case "$server_port" in
    ''|*[!0-9]*) fail '--port 必须为 1 到 65535 的整数' ;;
esac
[ "${#server_port}" -le 5 ] || fail '--port 必须为 1 到 65535 的整数'
[ "$server_port" -ge 1 ] && [ "$server_port" -le 65535 ] || fail '--port 超出有效范围'
[ "$(id -u)" -eq 0 ] || fail '请使用 sudo 或切换到 root 会话运行'

for command_name in openssl zip awk cmp install mktemp; do
    command -v "$command_name" >/dev/null 2>&1 || fail "缺少命令：$command_name"
done

# 默认使用文档约定的目录；OPENVPN_ROOT 可用于隔离的测试环境。
openvpn_root=${OPENVPN_ROOT:-/etc/openvpn}
case "$openvpn_root" in /*) ;; *) fail 'OPENVPN_ROOT 必须为绝对路径' ;; esac
easy_rsa_dir="$openvpn_root/easy-rsa"
pki_dir="$easy_rsa_dir/pki"
server_config="$openvpn_root/server/server.conf"
output_dir="$openvpn_root/clients"
archive_path="$output_dir/${client_name}-${platform}.zip"
lock_dir="$easy_rsa_dir/.generate-client.lock"

[ -x "$easy_rsa_dir/easyrsa" ] || fail "找不到可执行的 $easy_rsa_dir/easyrsa"
for source_file in "$pki_dir/ca.crt" "$pki_dir/private/ca.key" \
    "$pki_dir/index.txt" "$server_config" "$openvpn_root/server/ta.key"; do
    [ -f "$source_file" ] || fail "缺少服务端文件：$source_file；先完成服务端部署"
done

# 只读取配置字段，不执行配置内容；独立注释和行尾注释不会被当作指令。
read_server_option() {
    awk -v option="$1" -v field="$2" '
        $1 == option { value = $field; gsub(/["\047]/, "", value) }
        END { print value }
    ' "$server_config"
}

tls_auth_key=$(read_server_option tls-auth 2)
tls_crypt_key=$(read_server_option tls-crypt 2)
client_direction=''
if [ -n "$tls_auth_key" ] && [ -n "$tls_crypt_key" ]; then
    fail '服务端同时配置了 tls-auth 和 tls-crypt，请先确认实际使用的模式'
elif [ -n "$tls_auth_key" ]; then
    control_mode='tls-auth'
    control_key=$tls_auth_key
    server_direction=$(read_server_option tls-auth 3)
    case "$server_direction" in '#'*|';'*) server_direction='' ;; esac
    if [ -z "$server_direction" ]; then
        server_direction=$(read_server_option key-direction 2)
    fi
    case "$server_direction" in
        0) client_direction=1 ;;
        1) client_direction=0 ;;
        '') ;;
        *) fail '服务端 tls-auth 的方向参数必须为 0 或 1，也可两端均不指定' ;;
    esac
elif [ -n "$tls_crypt_key" ]; then
    control_mode='tls-crypt'
    control_key=$tls_crypt_key
else
    fail '服务端未配置 tls-auth 或 tls-crypt，无法按本方案生成匹配的客户端'
fi
case "$control_key" in
    ta.key|./ta.key|"$openvpn_root/server/ta.key") ;;
    *) fail '服务端控制通道密钥应为 server 目录中的 ta.key，请先核对配置路径' ;;
esac
auth_digest=$(read_server_option auth 2)
auth_digest=${auth_digest:-SHA1}
case "$auth_digest" in
    *[!a-zA-Z0-9_-]*) fail '服务端 auth 参数格式不正确' ;;
esac

# 客户端传输协议自动跟随服务端 proto，避免两端协议不一致导致握手超时。
server_proto=$(read_server_option proto 2)
case "$server_proto" in
    udp|udp4|udp6) client_proto=udp ;;
    tcp|tcp4|tcp6|tcp-server) client_proto=tcp ;;
    '') fail '服务端未配置 proto，无法确定客户端传输协议' ;;
    *) fail '服务端 proto 参数不正确，应为 udp 或 tcp' ;;
esac

install -d -m 700 "$output_dir"

# 同一时间只允许一个导出进程操作 PKI，避免并发签发写坏记录。
mkdir "$lock_dir" 2>/dev/null || fail "已有生成任务在运行；确认无任务后可用 rmdir $lock_dir 清理残留锁"
work_dir=''
cleanup() {
    if [ -n "$work_dir" ]; then
        rm -f "$work_dir/ca.crt" "$work_dir/client.crt" "$work_dir/client.key" \
            "$work_dir/ta.key" "$work_dir/client.conf" "$work_dir/client.ovpn" \
            "$work_dir/install.sh" \
            "$work_dir/cert.pub" "$work_dir/key.pub" "$work_dir/client.zip"
        rmdir "$work_dir"
    fi
    rmdir "$lock_dir"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

if [ -e "$archive_path" ] && [ "$force" -ne 1 ]; then
    fail "ZIP 已存在：$archive_path；如需重新导出，加 --force"
fi

certificate="$pki_dir/issued/$client_name.crt"
private_key="$pki_dir/private/$client_name.key"
request="$pki_dir/reqs/$client_name.req"

if [ -f "$certificate" ] && [ -f "$private_key" ]; then
    printf '复用设备证书：%s\n' "$client_name"
elif [ -e "$certificate" ] || [ -e "$private_key" ] || [ -e "$request" ]; then
    fail '该身份存在不完整的证书、私钥或请求，请先检查 PKI；脚本不会删除或覆盖它们'
else
    # Easy-RSA 吊销后可能移走原文件，不能把这种情况当成首次签发。
    if awk -F '\t' -v cn="$client_name" '
        { count = split($6, fields, "/")
          for (i = 1; i <= count; i++) if (fields[i] == "CN=" cn) found = 1 }
        END { exit(!found) }
    ' "$pki_dir/index.txt"; then
        fail '该名称已有签发记录但原文件缺失，可能已吊销；请检查 PKI 或使用新的设备身份'
    fi
    printf '签发设备证书：%s；请按提示输入 CA 密码\n' "$client_name"
    (
        cd "$easy_rsa_dir"
        # 不继承其他签发任务的私钥输出口令，确保 nopass 生成无口令设备私钥。
        unset EASYRSA_PASSOUT
        EASYRSA_BATCH=1 ./easyrsa build-client-full "$client_name" nopass
    )
fi

# 校验证书用途、有效期、签发者和 PKI 吊销状态；不导出失效身份。
openssl verify -purpose sslclient -CAfile "$pki_dir/ca.crt" "$certificate" >/dev/null \
    || fail '客户端证书验证失败，请检查有效期、CA 和证书用途'
serial=$(openssl x509 -in "$certificate" -noout -serial)
serial=${serial#serial=}
awk -F '\t' -v serial="$serial" '
    toupper($4) == toupper(serial) { status = $1 }
    END { exit(status != "V") }
' "$pki_dir/index.txt" || fail '证书未处于有效状态，可能已吊销'

work_dir=$(mktemp -d "$output_dir/.${client_name}.XXXXXX")
# 使用空口令检查私钥，避免导出后无人值守启动被口令提示阻塞。
openssl pkey -in "$private_key" -passin pass: -pubout > "$work_dir/key.pub" \
    || fail '私钥无法读取或受口令保护，不能按本方案无人值守启动'
openssl x509 -in "$certificate" -pubkey -noout > "$work_dir/cert.pub"
cmp -s "$work_dir/cert.pub" "$work_dir/key.pub" || fail '客户端证书与私钥不匹配'

# 不论设备身份和系统类型如何，包内证书、密钥名称固定。
install -m 600 "$pki_dir/ca.crt" "$work_dir/ca.crt"
openssl x509 -in "$certificate" -outform PEM > "$work_dir/client.crt"
install -m 600 "$private_key" "$work_dir/client.key"
install -m 600 "$openvpn_root/server/ta.key" "$work_dir/ta.key"

if [ "$platform" = 'windows' ]; then
    config_name='client.ovpn'
    cert_prefix=''
    zip_files='ca.crt client.crt client.key ta.key client.ovpn'
else
    config_name='client.conf'
    cert_prefix='/etc/openvpn/'
    zip_files='ca.crt client.crt client.key ta.key client.conf install.sh'
fi
control_channel_line="$control_mode ${cert_prefix}ta.key"
if [ -n "$client_direction" ]; then
    control_channel_line="$control_channel_line $client_direction"
fi

cat > "$work_dir/$config_name" <<EOF
# 服务端入口，协议与端口必须与服务端配置一致
client
dev tun
proto $client_proto
remote 121.40.72.224 $server_port
resolv-retry infinite
nobind
persist-key
persist-tun

# Windows 文件与配置同目录；Linux 文件统一安装到 /etc/openvpn
ca ${cert_prefix}ca.crt
cert ${cert_prefix}client.crt
key ${cert_prefix}client.key
# 自动匹配服务端的 tls-auth / tls-crypt；tls-auth 的方向与服务端相反
$control_channel_line
# 与服务端使用同一 HMAC 摘要，tls-auth 模式下尤其需要保持一致
auth $auth_digest

# 验证服务端证书用途与 CN；本方案的服务端证书 CN 为 server
remote-cert-tls server
verify-x509-name server name
tls-version-min 1.2
data-ciphers AES-256-GCM:AES-128-GCM
allow-compression no
verb 3
EOF

# Linux 包附带一键安装脚本：任意目录解压后执行即可完成安装、后台启动和开机自启。
if [ "$platform" = 'linux' ]; then
    cat > "$work_dir/install.sh" <<'INSTALL'
#!/bin/sh
# EMS OpenVPN 客户端一键安装：拷贝配置 -> 安装 openvpn -> systemd 后台启动并开机自启
# 用法：把 ZIP 解压到任意目录后执行  sudo ./install.sh
set -eu

fail() {
    printf '错误：%s\n' "$*" >&2
    exit 1
}

[ "$(id -u)" -eq 0 ] || fail '请使用 sudo 运行：sudo ./install.sh'

# 按脚本自身位置定位文件，因此解压在哪个目录都可以
src_dir=$(cd "$(dirname "$0")" && pwd)
for f in ca.crt client.crt client.key ta.key client.conf; do
    [ -f "$src_dir/$f" ] || fail "缺少文件 $f；请把 ZIP 完整解压到同一目录后再运行"
done

# 已有旧服务先停止，避免占用 TUN 设备后覆盖配置
if systemctl is-active --quiet ems-openvpn-client 2>/dev/null; then
    systemctl stop ems-openvpn-client
fi

# 安装 openvpn（已安装则跳过）
if ! command -v openvpn >/dev/null 2>&1; then
    if command -v apt-get >/dev/null 2>&1; then
        DEBIAN_FRONTEND=noninteractive apt-get update || true
        DEBIAN_FRONTEND=noninteractive apt-get install -y openvpn
    elif command -v dnf >/dev/null 2>&1; then
        dnf install -y openvpn
    elif command -v yum >/dev/null 2>&1; then
        yum install -y openvpn
    else
        fail '未找到 openvpn 且无法识别包管理器，请先手动安装 openvpn 再运行本脚本'
    fi
fi
openvpn_bin=$(command -v openvpn)

# 安装位置固定为 /etc/openvpn，client.conf 内引用的就是这些绝对路径
install -d -m 700 /etc/openvpn
install -m 600 "$src_dir/client.conf" /etc/openvpn/client.conf
install -m 600 "$src_dir/client.key" /etc/openvpn/client.key
install -m 600 "$src_dir/ta.key" /etc/openvpn/ta.key
install -m 644 "$src_dir/ca.crt" /etc/openvpn/ca.crt
install -m 644 "$src_dir/client.crt" /etc/openvpn/client.crt

# 创建 systemd 服务：后台运行、断线自动重连、开机自启
cat > /etc/systemd/system/ems-openvpn-client.service <<UNIT
[Unit]
Description=EMS OpenVPN client
Wants=network-online.target
After=network-online.target

[Service]
Type=simple
ExecStart=$openvpn_bin --cd /etc/openvpn --config /etc/openvpn/client.conf
Restart=on-failure
RestartSec=10

[Install]
WantedBy=multi-user.target
UNIT

systemctl daemon-reload
systemctl enable ems-openvpn-client >/dev/null
if ! systemctl restart ems-openvpn-client; then
    fail '服务启动失败；执行 journalctl -u ems-openvpn-client -n 50 --no-pager 查看原因'
fi

printf '安装完成：服务已在后台运行，并已设置开机自启。\n'
printf '查看状态：systemctl status ems-openvpn-client --no-pager\n'
printf '查看日志：journalctl -u ems-openvpn-client -n 50 --no-pager\n'
printf '连通验证：ip -brief address && ping -c 4 10.8.0.1\n'
INSTALL
    chmod 755 "$work_dir/install.sh"
fi

# 只打包所需文件（Windows 五个，Linux 六个），不包含 CA 私钥、服务端私钥或临时检查文件。
(
    cd "$work_dir"
    zip -q client.zip $zip_files
)
chmod 600 "$work_dir/client.zip"
mv -f "$work_dir/client.zip" "$archive_path"
printf '生成完成：%s\n' "$archive_path"
printf '包内配置：%s；设备证书 CN：%s\n' "$config_name" "$client_name"
printf '传输协议：%s；控制通道：%s；认证摘要：%s\n' "$client_proto" "$control_mode" "$auth_digest"

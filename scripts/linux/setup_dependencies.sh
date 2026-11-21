#!/usr/bin/env bash
# EMS 在 Ubuntu 24.04 LTS（noble）上的依赖安装脚本。
#
#   sudo ./scripts/linux/setup_dependencies.sh                 安装构建依赖与运行服务
#   sudo ./scripts/linux/setup_dependencies.sh build           仅构建依赖（编译器、CMake、客户端库）
#   sudo ./scripts/linux/setup_dependencies.sh runtime         仅 MySQL / Redis / TDengine
#   sudo ./scripts/linux/setup_dependencies.sh --cross         额外安装 ARM64 交叉工具链
#   sudo ./scripts/linux/setup_dependencies.sh runtime --no-tdengine
#
# 版本默认与开发机一致：MySQL 8.0.46、Redis 7.0.15、TDengine 3.4.3.9。
# 可用环境变量覆盖：MYSQL_VERSION、REDIS_VERSION、TDENGINE_VERSION、TDENGINE_MIRROR。
# 源内没有目标版本时默认退回候选版本并告警；STRICT_VERSIONS=1 则直接失败。
set -euo pipefail

MYSQL_VERSION="${MYSQL_VERSION:-8.0.46-0ubuntu0.24.04.5}"
REDIS_VERSION="${REDIS_VERSION:-5:7.0.15-1ubuntu0.24.04.5}"
TDENGINE_VERSION="${TDENGINE_VERSION:-3.4.3.9}"
TDENGINE_MD5="${TDENGINE_MD5:-7b6baf02151dc49488b2175f284d026e}"
TDENGINE_MIRROR="${TDENGINE_MIRROR:-https://downloads.taosdata.com/tdengine-tsdb-oss}"
TDENGINE_VERIFY="${TDENGINE_VERIFY:-1}"
STRICT_VERSIONS="${STRICT_VERSIONS:-0}"
ENABLE_SERVICES="${ENABLE_SERVICES:-1}"
WITH_TDENGINE="${WITH_TDENGINE:-1}"
INSTALL_CROSS="${INSTALL_CROSS:-0}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLEANUP_PATHS=()
INSTALLED_SERVICES=()
APT_REFRESHED=0

log() { printf '\n==> %s\n' "$*"; }
note() { printf '    %s\n' "$*"; }
warn() { printf '警告：%s\n' "$*" >&2; }
die() {
  printf '错误：%s\n' "$*" >&2
  exit 1
}

cleanup() {
  local path
  for path in "${CLEANUP_PATHS[@]:-}"; do
    if [[ -n "$path" && -d "$path" ]]; then
      rm -rf "$path"
    fi
  done
}
trap cleanup EXIT

usage() {
  cat <<'EOF'
用法：sudo ./scripts/linux/setup_dependencies.sh [all|build|runtime] [选项]

  all        安装构建依赖与运行服务（默认）
  build      仅构建依赖：编译器、CMake、OpenSSL、hiredis、libmodbus、MySQL 客户端库
  runtime    仅运行服务：MySQL、Redis、TDengine

  --cross         追加 ARM64 交叉工具链与 ARM64 Python 开发包
  --no-tdengine   运行服务阶段跳过 TDengine
  --no-enable     安装后不启动、不自启 MySQL/Redis/TDengine
  --strict        要求 APT 源中存在精确版本，否则失败
  -h, --help      显示本帮助
EOF
}

TARGET="all"
if [[ $# -gt 0 && "$1" != -* ]]; then
  TARGET="$1"
  shift
fi
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cross) INSTALL_CROSS=1 ;;
    --no-tdengine) WITH_TDENGINE=0 ;;
    --no-enable) ENABLE_SERVICES=0 ;;
    --strict) STRICT_VERSIONS=1 ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      warn "未知参数：$1"
      usage
      exit 1
      ;;
  esac
  shift
done

case "$TARGET" in
  all | build | runtime) ;;
  *)
    warn "未知目标：$TARGET"
    usage
    exit 1
    ;;
esac

# ---------------------------------------------------------------- 环境检查

if [[ "${EUID}" -ne 0 ]]; then
  die "请以 root 运行：sudo $0 $TARGET"
fi

if [[ -r /etc/os-release ]]; then
  # shellcheck disable=SC1091
  . /etc/os-release
  if [[ "${ID:-}" != "ubuntu" || "${VERSION_ID:-}" != "24.04" ]]; then
    warn "当前系统为 ${PRETTY_NAME:-未知}，脚本以 Ubuntu 24.04 为基准，继续执行。"
  fi
  note "系统：${PRETTY_NAME:-Ubuntu 24.04}"
fi

if ! command -v apt-get >/dev/null 2>&1; then
  die "未找到 apt-get，本脚本仅支持 Debian/Ubuntu 系发行版。"
fi

# ---------------------------------------------------------------- APT 辅助

ensure_apt_index() {
  if [[ "${APT_REFRESHED}" -eq 0 ]]; then
    log "刷新 APT 索引"
    apt-get update -qq
    APT_REFRESHED=1
  fi
}

apt_install() {
  ensure_apt_index
  DEBIAN_FRONTEND=noninteractive apt-get install -y \
    --allow-change-held-packages --allow-downgrades "$@"
}

# 安装指定版本；源内没有该版本时按候选版本安装（STRICT_VERSIONS=1 时失败）。
apt_install_pinned() {
  local pkg="$1" want="$2" candidate
  ensure_apt_index
  candidate="$(apt-cache policy "$pkg" 2>/dev/null | awk '/Candidate:/ {print $2; exit}')"
  if [[ -z "$candidate" || "$candidate" == "(none)" ]]; then
    die "APT 源中找不到 $pkg，请检查 /etc/apt/sources.list.d 与网络连通性。"
  fi
  if [[ "$candidate" == "$want" ]]; then
    apt_install "$pkg=$want"
    return
  fi
  if [[ "${STRICT_VERSIONS}" -eq 1 ]]; then
    die "$pkg 需要 $want，当前源候选为 $candidate；请先 apt-get update，或取消 STRICT_VERSIONS。"
  fi
  warn "$pkg 源内候选为 $candidate（目标 $want），按候选版本安装。"
  apt_install "$pkg"
}

# ---------------------------------------------------------------- 构建依赖

install_build_dependencies() {
  log "[1/3] 安装构建依赖（编译器、CMake、第三方库）"
  # hiredis 在 Linux 上默认不按需构建，必须由系统提供；libmysqlclient 供 ems-web/ems-collector 链接。
  apt_install \
    build-essential cmake ninja-build pkg-config git curl ca-certificates xz-utils \
    libssl-dev zlib1g-dev libmodbus-dev libhiredis-dev libsqlite3-dev \
    python3-dev python3-venv

  log "[2/3] 安装 MySQL 客户端开发包（$MYSQL_VERSION）"
  apt_install_pinned libmysqlclient-dev "$MYSQL_VERSION"
}

install_cross_dependencies() {
  log "安装 ARM64 交叉工具链"
  apt_install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
  if [[ -x "${SCRIPT_DIR}/setup_ubuntu_arm64_apt.sh" ]]; then
    log "安装 ARM64 Python 开发包"
    "${SCRIPT_DIR}/setup_ubuntu_arm64_apt.sh"
  else
    warn "未找到 ${SCRIPT_DIR}/setup_ubuntu_arm64_apt.sh，跳过 ARM64 Python 开发包。"
  fi
}

# ---------------------------------------------------------------- 运行服务

install_mysql() {
  log "安装 MySQL Server $MYSQL_VERSION"
  apt_install_pinned mysql-server "$MYSQL_VERSION"
  apt_install_pinned mysql-client "$MYSQL_VERSION"
  INSTALLED_SERVICES+=(mysql)
}

install_redis() {
  log "安装 Redis $REDIS_VERSION"
  apt_install_pinned redis-server "$REDIS_VERSION"
  apt_install_pinned redis-tools "$REDIS_VERSION"
  INSTALLED_SERVICES+=(redis-server)
}

# TDengine 只提供 tar.xz 包，解压后执行官方 install.sh，再补 SDK 查找路径。
install_tdengine() {
  local package="tdengine-tsdb-oss-${TDENGINE_VERSION}-linux-x64.tar.xz"
  local url="${TDENGINE_MIRROR%/}/${TDENGINE_VERSION}/${package}"
  local work source

  log "[3/3] 安装 TDengine OSS $TDENGINE_VERSION"
  work="$(mktemp -d "${TMPDIR:-/tmp}/ems-tdengine.XXXXXX")"
  CLEANUP_PATHS+=("$work")

  note "下载 $url"
  if ! curl -fL --retry 3 --retry-delay 2 --connect-timeout 20 -o "${work}/${package}" "$url"; then
    die "TDengine 安装包下载失败：$url"
  fi

  if [[ "${TDENGINE_VERIFY}" -eq 1 ]]; then
    note "校验 MD5 ${TDENGINE_MD5}"
    if ! (cd "$work" && printf '%s  %s\n' "$TDENGINE_MD5" "$package" | md5sum -c - >/dev/null); then
      note "实际 MD5：$(md5sum "${work}/${package}" | awk '{print $1}')"
      die "TDengine 安装包校验失败；如确认来源可信，可用 TDENGINE_VERIFY=0 跳过校验。"
    fi
  fi

  tar -xJf "${work}/${package}" -C "$work"
  source="${work}/tdengine-tsdb-oss-${TDENGINE_VERSION}"
  if [[ ! -x "${source}/install.sh" ]]; then
    die "安装包结构与预期不符：${source}/install.sh 不存在。"
  fi

  # -e no：跳过 FQDN 等交互提问，便于无人值守安装。
  (cd "$source" && ./install.sh -e no)

  link_tdengine_sdk
  INSTALLED_SERVICES+=(taosd)
}

# CMake 在 Linux 只搜索 /usr/include、/usr/local/include 与 /usr/local/lib。
link_tdengine_sdk() {
  local include_dir="/usr/local/taos/include"
  local driver_dir="/usr/local/taos/driver"

  if [[ -f "${include_dir}/taos.h" ]]; then
    if [[ -e /usr/local/include/taos.h && ! -L /usr/local/include/taos.h ]]; then
      note "/usr/local/include/taos.h 已存在（非软链），保留原文件。"
    else
      ln -sfn "${include_dir}/taos.h" /usr/local/include/taos.h
      note "链接 /usr/local/include/taos.h -> ${include_dir}/taos.h"
    fi
  fi

  if [[ -f "${driver_dir}/libtaos.so" ]]; then
    if [[ -e /usr/local/lib/libtaos.so && ! -L /usr/local/lib/libtaos.so ]]; then
      note "/usr/local/lib/libtaos.so 已存在（非软链），保留原文件。"
    else
      ln -sfn "${driver_dir}/libtaos.so" /usr/local/lib/libtaos.so
      note "链接 /usr/local/lib/libtaos.so -> ${driver_dir}/libtaos.so"
    fi
    printf '%s\n' "$driver_dir" >/etc/ld.so.conf.d/ems-tdengine.conf
    ldconfig
  fi
}

install_runtime_dependencies() {
  install_mysql
  install_redis
  if [[ "${WITH_TDENGINE}" -eq 1 ]]; then
    install_tdengine
  else
    note "已跳过 TDengine（--no-tdengine）。"
  fi
  enable_services
}

enable_services() {
  local service
  if ! command -v systemctl >/dev/null 2>&1; then
    note "未检测到 systemd，跳过服务启动。"
    return
  fi
  for service in "${INSTALLED_SERVICES[@]:-}"; do
    if [[ -z "$service" ]]; then
      continue
    fi
    if ! systemctl cat "$service" >/dev/null 2>&1; then
      warn "未找到 ${service} 的 systemd 单元，跳过。"
      continue
    fi
    if [[ "${ENABLE_SERVICES}" -eq 0 ]]; then
      systemctl disable --now "$service" >/dev/null 2>&1 || true
      note "已停止 ${service}（--no-enable）。"
      continue
    fi
    if systemctl enable --now "$service" >/dev/null 2>&1; then
      note "已启动 ${service}"
    else
      warn "${service} 启动失败，可用 journalctl -u ${service} 排查。"
    fi
  done
}

# ---------------------------------------------------------------- 结果核对

report_versions() {
  log "版本核对"
  if command -v mysql >/dev/null 2>&1; then
    note "$(mysql --version)"
  fi
  if command -v redis-server >/dev/null 2>&1; then
    note "$(redis-server --version | cut -d' ' -f1-3)"
  fi
  if command -v taosd >/dev/null 2>&1; then
    note "$(taosd -V 2>&1 | head -n1 || true)"
  fi

  cat <<'EOF'

构建：
  ./scripts/linux/build_linux_arm.sh native Release

ARM64 交叉（先装交叉工具链与 ARM64 Python 开发包）：
  sudo ./scripts/linux/setup_dependencies.sh --cross
  ./scripts/linux/build_linux_arm.sh cross Release

CMake 未自动找到 TDengine SDK 时显式传入：
  -DEMS_DATABASE_TAOS_INCLUDE_DIR=/usr/local/taos/include
  -DEMS_DATABASE_TAOS_LIBRARY=/usr/local/taos/driver/libtaos.so
EOF
}

# ---------------------------------------------------------------- 入口

run_build=0
run_runtime=0
if [[ "$TARGET" == "all" || "$TARGET" == "build" ]]; then
  run_build=1
fi
if [[ "$TARGET" == "all" || "$TARGET" == "runtime" ]]; then
  run_runtime=1
fi

if [[ "$run_build" -eq 1 ]]; then
  install_build_dependencies
fi

if [[ "${INSTALL_CROSS}" -eq 1 ]]; then
  install_cross_dependencies
fi

if [[ "$run_runtime" -eq 1 ]]; then
  install_runtime_dependencies
fi

report_versions

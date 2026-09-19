#!/usr/bin/env bash
#
# EMS Backend 代码格式化入口（clang-format，基于 Google 风格）。
#
# 规则文件：仓库根目录 .clang-format
# 第三方目录 backend/3rd/ 永不参与格式化。
#
# 用法：
#   ./scripts/format.sh                      格式化 backend 下全部自有代码
#   ./scripts/format.sh --check              仅检查（CI/提交前用），不改动文件
#   ./scripts/format.sh --diff               打印 unified diff，不改动文件
#   ./scripts/format.sh --check backend/src/net   只检查指定目录/文件
#   ./scripts/format.sh -j 8                 指定并行度（默认 CPU 核数）
#
# 环境变量：
#   CLANG_FORMAT   指定 clang-format 可执行文件路径（默认从 PATH 中查找）
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

# 默认格式化范围：只包含项目自有代码，不含 3rd/
DEFAULT_PATHS=(backend/include backend/src backend/tests backend/examples)
# 兜底排除：即使显式传入 backend/ 也会跳过该目录下的第三方代码
PRUNE_DIR="backend/3rd"

MODE="write"
JOBS=""
EXTRA_ARGS=()
PATHS=()

usage() {
  sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^#\{1,2\} \{0,1\}//'
  exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -c|--check)   MODE="check"; shift ;;
    -d|--diff)    MODE="diff"; shift ;;
    -j|--jobs)    JOBS="${2:?missing value for $1}"; shift 2 ;;
    -h|--help)    usage 0 ;;
    -*)           echo "未知参数: $1" >&2; usage 1 ;;
    *)            PATHS+=("$1"); shift ;;
  esac
done

# ---------------------------------------------------------------------------
# 定位 clang-format
# ---------------------------------------------------------------------------
find_clang_format() {
  if [[ -n "${CLANG_FORMAT:-}" ]]; then
    if [[ -x "${CLANG_FORMAT}" ]]; then echo "${CLANG_FORMAT}"; return 0; fi
    echo "CLANG_FORMAT 指向的文件不可执行: ${CLANG_FORMAT}" >&2
    return 1
  fi
  # 优先无后缀版本，其次从高到低找带版本号的
  local candidates=(clang-format)
  local v
  for v in 21 20 19 18 17 16 15 14; do
    candidates+=("clang-format-${v}")
  done
  local c
  for c in "${candidates[@]}"; do
    if command -v "${c}" >/dev/null 2>&1; then echo "${c}"; return 0; fi
  done
  return 1
}

if ! CLANG_FORMAT_BIN="$(find_clang_format)"; then
  cat >&2 <<'EOF'
未找到 clang-format。请任选一种方式安装：

  pip  （推荐，跨平台且版本可控）
      python -m pip install clang-format==21.1.8

  Ubuntu / Debian
      sudo apt-get install -y clang-format

  macOS
      brew install clang-format

安装后确认：clang-format --version
EOF
  exit 127
fi

CF_VERSION="$("${CLANG_FORMAT_BIN}" --version 2>&1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || echo 'unknown')"
CF_MAJOR="${CF_VERSION%%.*}"

# 项目固定的 clang-format 版本。
# clang-format 的对齐细节（尤其是中文行尾注释的列宽计算）在版本间存在差异，
# 用非固定版本执行 --check 会产生假告警，所以这里显式提示。
PINNED_CF_VERSION="22.1.8"

# 低于 18 的版本不支持根目录 .clang-format-ignore，会误改 vendored 单头库，直接拒绝
if [[ "${CF_MAJOR}" =~ ^[0-9]+$ ]] && (( CF_MAJOR < 18 )); then
  echo "clang-format 版本过低（${CF_VERSION}），需要 18 及以上。" >&2
  echo "原因：根目录 .clang-format-ignore 的忽略机制自 18 起才支持，" >&2
  echo "      低版本会误改 backend/include/util/rapidcsv.h 等第三方单头库。" >&2
  echo "建议：python -m pip install clang-format==${PINNED_CF_VERSION}" >&2
  exit 1
fi

if [[ "${CF_VERSION}" != "${PINNED_CF_VERSION}" ]]; then
  echo "提示：当前 clang-format 为 ${CF_VERSION}，项目固定版本为 ${PINNED_CF_VERSION}。" >&2
  echo "      版本不一致时 ${MODE} 结果可能与团队其他机器有差异，" >&2
  echo "      建议执行：python -m pip install clang-format==${PINNED_CF_VERSION}" >&2
  echo >&2
fi

# ---------------------------------------------------------------------------
# 收集待处理文件
# ---------------------------------------------------------------------------
if [[ ${#PATHS[@]} -eq 0 ]]; then
  PATHS=("${DEFAULT_PATHS[@]}")
fi

FILE_LIST="$(mktemp)"
trap 'rm -f "${FILE_LIST}"' EXIT

collect_files() {
  local p
  for p in "${PATHS[@]}"; do
    if [[ ! -e "${p}" ]]; then
      echo "路径不存在，已跳过: ${p}" >&2
      continue
    fi
    if [[ -f "${p}" ]]; then
      echo "${p}"
      continue
    fi
    find "${p}" \
      \( -path "${PRUNE_DIR}" -o -path "*/${PRUNE_DIR##*/}/*" \) -prune -o \
      -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.c' -o -name '*.cc' -o -name '*.cpp' \) \
      -print
  done
}

collect_files | sort -u > "${FILE_LIST}"

TOTAL="$(wc -l < "${FILE_LIST}" | tr -d ' ')"
if [[ "${TOTAL}" == "0" ]]; then
  echo "没有找到需要处理的 C/C++ 文件。"
  exit 0
fi

if [[ -z "${JOBS}" ]]; then
  if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
  elif command -v sysctl >/dev/null 2>&1; then
    JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
  else
    JOBS=4
  fi
fi

echo "clang-format : ${CLANG_FORMAT_BIN} (${CF_VERSION})"
echo "规则文件     : ${ROOT_DIR}/.clang-format"
echo "处理文件数   : ${TOTAL}"
echo "排除目录     : ${PRUNE_DIR}"
echo

# ---------------------------------------------------------------------------
# 执行
# ---------------------------------------------------------------------------
case "${MODE}" in
  write)
    # shellcheck disable=SC2016
    xargs -a "${FILE_LIST}" -d '\n' -P "${JOBS}" -n 20 \
      "${CLANG_FORMAT_BIN}" -i --style=file
    echo "完成：已就地格式化 ${TOTAL} 个文件。"
    ;;
  check)
    STATUS=0
    while IFS= read -r f; do
      if ! "${CLANG_FORMAT_BIN}" --style=file --dry-run --Werror "${f}" 2>&1; then
        STATUS=1
      fi
    done < "${FILE_LIST}"
    if [[ "${STATUS}" -eq 0 ]]; then
      echo "检查通过：所有文件均符合 .clang-format 规则。"
    else
      echo >&2
      echo "检查未通过。执行 ./scripts/format.sh 自动修复上述文件。" >&2
    fi
    exit "${STATUS}"
    ;;
  diff)
    while IFS= read -r f; do
      "${CLANG_FORMAT_BIN}" --style=file "${f}" | diff -u --label "a/${f}" --label "b/${f}" "${f}" - || true
    done < "${FILE_LIST}"
    ;;
esac

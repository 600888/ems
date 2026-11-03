# 代码格式化规范（clang-format）

本文说明 `backend/` 下 C/C++ 代码的格式化规则、工具链接入方式和已知边界。规则以仓库根目录 `.clang-format` 为准。全项目入口同时处理自有 CMake 和前端源码，用法以仓库根目录 README 的“代码格式化”为准。

## 适用范围

| 范围 | 是否格式化 | 说明 |
| --- | --- | --- |
| `backend/include` `backend/src` `backend/tests` `backend/examples` | 是 | 项目自有代码 |
| `backend/third_party/**` | 否 | 第三方依赖，跟随上游版本更新 |

`third_party/` 目录内另有 `.clang-format`（`DisableFormat: true`）作为兜底，防止 IDE 或编辑器自动格式化误改第三方代码。

> 注意：`third_party/iec104/depends/catch` 与 `lib60870` 自带 `.clang-format`，就近配置会覆盖上面的兜底文件。执行脚本与 CMake 目标不会进入 `third_party/`，因此不受影响；但如果编辑器对这两个子目录开启「保存时格式化」，仍可能误改。**不要对 `backend/third_party/` 打开保存时格式化。**

## 工具版本

clang-format 的对齐细节（尤其是中文行尾注释的列宽计算）在版本间存在差异，因此**固定使用 22.1.8**：

```bash
python -m pip install clang-format==22.1.8
```

该版本在 Windows(x64/arm64)、macOS、Linux(x86_64/aarch64/armv7) 均有 wheel，`backend/` 与 ARM 开发机可保持同一版本。使用其他版本不会报错，但会提示，且 `format-check` 可能出现假告警。

## 使用方式

### 全项目入口（Windows / Linux / macOS）

在仓库根目录执行：

```bash
python -m pip install --target .cache/format-tools -r scripts/requirements-format.txt
npm --prefix frontend ci
python scripts/format.py
python scripts/format.py --check
python scripts/format.py --diff
python scripts/format.py --scope cpp backend/src/web
```

入口只收集自有源码，跳过依赖、构建与缓存目录；CMake 使用 `.cmake-format.yaml`，Vue/TS/CSS 使用 `.prettierrc.json`。仅格式化前端可执行 `npm --prefix frontend run format`。

### 既有 Bash 脚本（仅 C/C++）

```bash
./scripts/format.sh                    # 就地格式化全部自有代码
./scripts/format.sh --check            # 仅检查，不符合时以非 0 退出
./scripts/format.sh --diff             # 打印 unified diff，不改动文件
./scripts/format.sh --check backend/src/net     # 只处理指定目录或文件
./scripts/format.sh -j 8               # 指定并行度
```

脚本通过 `CLANG_FORMAT` 环境变量可指定可执行文件路径。

### CMake 目标

```bash
cd backend && mkdir -p build && cd build
cmake -DCLANG_FORMAT_EXECUTABLE=/path/to/clang-format ..
make format          # 就地格式化
make format-check    # 仅检查
```

关闭方式：`cmake -DEMS_ENABLE_FORMAT_TARGET=OFF ..`。

### 提交前检查

```bash
git diff --name-only --cached --diff-filter=ACM \
  | grep -E '\.(h|hpp|c|cc|cpp)$' \
  | xargs -r ./scripts/format.sh --check
```

## 规则说明

基础为 Google C++ Style Guide，以下差异是有意为之：

| 选项 | Google 默认 | 本项目 | 原因 |
| --- | --- | --- | --- |
| `IndentWidth` | 2 | **4** | 沿用既有代码缩进，避免全量重排 |
| `ColumnLimit` | 80 | **100** | 既有代码绝大多数在 80 列内，放宽以减少无意义折行 |
| `ReflowComments` | `Always` | **`false`** | 项目内大量中文 Doxygen 注释，CJK 显示宽度计算在版本间不一致，重排结果不可控 |

其余关键约定均与 Google 一致：大括号同行、访问修饰符左移一格、`case` 缩进一级、指针贴左（`int* p`）、命名空间内容不缩进并补全 `}  // namespace xxx`、头文件按「主头文件 → C 系统 → C++ 标准 → 其他」分组排序、行尾注释前保留 2 个空格。

未显式开启的选项：`InsertBraces`（不自动为单语句分支补大括号）、`InsertNewlineAtEOF`（不为缺失末尾换行的文件补换行）。如需调整，改 `.clang-format` 后重新全量格式化即可。

## 行尾约定

`.clang-format` 中 `LineEnding: LF`，同时根目录 `.gitattributes` 声明 `* text=auto eol=lf`。

这两项必须成对存在：Windows 上 git 常配置 `core.autocrlf=true`，会把 checkout 出的文件转成 CRLF；若不固定 `eol=lf`，每次 checkout 后 `format-check` 都会误报，且每次提交都会产生整文件的行尾 diff。

## 格式化是有损的吗

clang-format 会调整空白、头文件顺序、命名空间行尾注释，以及在必要时为跨行宏补续行反斜杠。格式化后仍应执行对应构建和检查，尤其注意依赖包含顺序的旧代码。

可以接受的少数非空白改动：

- **宏定义折行**：超过列宽的 `#define` 会被拆成多行并补 `\`，展开结果不变。
- **长字符串字面量拆分**：仅 `backend/src/net/protocol/modbus/modbus_mapping_map.hpp` 受影响（8 处），相邻字面量在编译期拼接，运行结果不变；但按完整 message 文本 grep 时会失效，如不接受可将 `BreakStringLiterals` 设为 `false`。

## 常见问题

**为什么 `make format-check` 报的文件我本地看起来没问题？**
先确认 clang-format 版本为 22.1.8，再确认工作区文件行尾为 LF（`file <name>` 或 `git ls-files --eol -- <name>`）。CRLF 会被判定为不合规。

**改动别人的文件造成大 diff 怎么办？**
先只格式化自己改动的文件：`./scripts/format.sh <改动的文件列表>`。全量格式化只在需要时做一次，并单独提交。

**`backend/include/util/` 下的文件看起来是旧代码，也要格式化吗？**
该目录只保留项目自研通用工具头；第三方 `rapidcsv.h` 位于 `backend/third_party/rapidcsv/`，日志包装头位于 `backend/src/log/`。`date.hpp`、`time.hpp`、`date_time.hpp`、`timer.hpp`、`task.hpp`、`map.hpp`、`crc_check.h` 等旧风格文件仍在格式化范围内。Modbus 专用工具位于 `backend/src/net/protocol/modbus/`。其中 `crc_check.h` 依赖未定义的 `hUChar`、`modbus_mapping_map.hpp` 依赖未定义的 `ushort`，本身已无法编译，属于遗留代码，如需排除请加入 `.clang-format-ignore`。

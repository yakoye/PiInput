# PiInput 工作区、构建和发布目录规范

## 1. 唯一职责

- 源码：`C:\Users\color\Downloads\PiInput\PiInput-repo`
- 外部词库：`C:\Users\color\Downloads\PiInput\dicts`
- 安装交付：`C:\Users\color\Downloads\PiInput\releases`
- 历史归档：`C:\Users\color\Downloads\PiInput\archive`
- 内部证据：`PiInput-repo\artifacts`

`dicts` 必须与 `PiInput-repo` 同级。`CMakeLists.txt` 会读取 `..\dicts\cache\piinput-base.lex` 和 `..\dicts\generated\piinput-combined.tsv`，因此不能为了视觉整洁把它搬进归档。

## 2. 构建目录

- `build\windows-x64`：当前 Windows CMake 构建树；增量编译依赖它。
- `dist\windows-x64`：`cmake --install` 后的运行时暂存树；打包脚本从这里取文件。
- `archive\builds`：过期的 `generated-build-*`、`generated-old-build-*` 和专题构建树。旧 CMakeCache 带绝对路径，只用于追溯，不应继续增量编译。

主构建入口仍是仓库根目录的 `build.ps1`。它是兼容入口，不能仅为“看起来整齐”而改名。

## 3. 发布目录

| 状态 | 位置 | 规则 |
|---|---|---|
| 正式最新版 | `releases\current\vX.Y.Z` | 仅全部门禁通过后进入 |
| 候选版 | `releases\candidates\vX.Y.Z-commit12` | 必须带精确提交号；不得称正式版 |
| 历史版 | `releases\history\vX.Y.Z` | current 被新版本替换后移入 |
| 撤回版 | `releases\withdrawn` | 禁止误装；保留原因和哈希 |

仓库内 `artifacts` 是流水线工作区与证据区。即使里面出现 ZIP，也不表示它已正式发布。

## 4. 测试与证据

- `artifacts\ci`：CI 下载物。
- `artifacts\package-closure`：安装包静态/安装闭环证据。
- `artifacts\soak`：稳定性采样、夹具和历史结果。
- `artifacts\tests`：功能回归、语料、打字测试和 XML 报告。
- `artifacts\diagnostics`：调试 trace。
- `artifacts\verification`：包内容与历史版本验证。

正在写入的 8 小时目录不移动。判断标准不是目录日期，而是后台运行仍存在且 `summary.json` 尚未生成。

## 5. 统一工作流

```powershell
# 查看仓库、候选包和目录状态
pwsh ./scripts/workflows/piinput-workflow.ps1 status

# 编译并按原有 build.ps1 运行测试
pwsh ./scripts/workflows/piinput-workflow.ps1 build

# clean 后全量编译和测试
pwsh ./scripts/workflows/piinput-workflow.ps1 test -Clean

# 更新源码清单并创建本地提交；不会 push
pwsh ./scripts/workflows/piinput-workflow.ps1 commit -Message "feat: ..."

# 完整构建、测试、打包，并放入 candidates；不会正式发布
pwsh ./scripts/workflows/piinput-workflow.ps1 candidate -Version 0.7.15

# 只整理历史文件，不删除任何内容
pwsh ./scripts/workflows/piinput-workflow.ps1 organize
```

`promote` 只负责本地目录状态切换。它要求显式提供 `-GatesPassed`，但仍不代替签名验证、真实宿主验收、tag、GitHub Release 和公开资产核对。

## 6. 清理原则

1. 不删除来源不明的包或证据，只移动到归档。
2. 不移动 `PiInput-repo`、`dicts`、当前 `build`、`dist`。
3. Git 工作树必须通过 Git 迁移，不能直接拖动。
4. 未完成的 soak 不移动。
5. `releases/current` 永远只能有一个正式版本目录。


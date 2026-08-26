# scripts 脚本说明

日常优先使用统一入口：

```powershell
pwsh ./scripts/workflows/piinput-workflow.ps1 status
pwsh ./scripts/workflows/piinput-workflow.ps1 build
pwsh ./scripts/workflows/piinput-workflow.ps1 test -Clean
pwsh ./scripts/workflows/piinput-workflow.ps1 commit -Message "说明"
pwsh ./scripts/workflows/piinput-workflow.ps1 candidate -Version 0.7.15
pwsh ./scripts/workflows/install-latest-candidate.ps1 -DryRun
pwsh ./scripts/workflows/install-latest-candidate.ps1
```

## workflows：统一工作流

| 脚本 | 用途 |
|---|---|
| `piinput-workflow.ps1` | 查看状态、编译、测试、本地提交、生成带时间与提交号的候选、提升正式版本和整理工作区；不会自动 push 或发布 GitHub Release |
| `install-latest-candidate.ps1` | 开发机入口：按目录名时间选择最新候选，再调用通用一键更新脚本 |
| `PiInput-OneClick-Update.ps1` | 可随 ZIP 分发的独立更新器：校验 SHA-256 和包结构，卸载旧版、安装新版并核对 build ID；保留用户数据 |
| `一键更新PiInput.cmd` | 给普通用户双击的入口；与更新 PS1、ZIP、SHA-256 文件放在同一目录即可 |

### 给其他人分发一键更新包

同一文件夹只放以下四个文件：

1. `一键更新PiInput.cmd`
2. `PiInput-OneClick-Update.ps1`
3. `PiInput-vX.Y.Z-windows-x64.zip`
4. `PiInput-vX.Y.Z-windows-x64.zip.sha256.txt`

对方双击 `一键更新PiInput.cmd` 后，脚本会依次校验、卸载、安装和核对版本。脚本整体不以管理员身份运行；安装器需要注册机器级 TSF Shim 时会自行显示 UAC。旧版卸载默认不传 `--remove-user-data`，用户设置和词库会保留。候选生成流程会自动把两个更新脚本复制到候选目录。

## maintenance：工作区维护

| 脚本 | 用途 |
|---|---|
| `organize-workspace.ps1` | 分类历史 worktree、构建产物、CI、包闭环和 soak 证据；默认只移动不删除，未完成的 soak 不移动 |

## windows：Windows 构建与发布底层脚本

| 脚本 | 用途 |
|---|---|
| `release.ps1` | Windows 完整发布入口：版本同步、构建、CTest、安装树刷新和打包 |
| `package-release.ps1` | 从 `dist/windows-x64` 生成运行时目录、ZIP 和 SHA-256 |
| `package-portable-test.ps1` | 生成便携测试包 |
| `verify-package-closure.ps1` | 校验包白名单、版本、build ID、签名策略和可选安装/重装/卸载闭环 |
| `verify-release-evidence.ps1` | 汇总发布门禁证据，阻止缺证据发布 |
| `sign-binaries.ps1` | 使用 PFX 和时间戳服务签署 PE 文件并输出报告 |
| `compose-test-result.ps1` | 把多个测试结果合成为统一机器可读报告 |
| `generate-typing-script.ps1` | 生成长文本逐键输入脚本 |
| `resolve-installed-dev.ps1` | 安全解析当前用户安装的活动 Host、Shim、版本目录 |
| `update-source-manifest.ps1` | 刷新 `FILE_LIST.txt` 与 `SHA256SUMS.txt` |

## dev：开发机安装、配置和诊断

| 脚本 | 用途 |
|---|---|
| `setup-dev.ps1` / `install-dev.ps1` | 准备或安装开发版 |
| `refresh-installed-dev.ps1/.cmd` | 刷新已安装开发版 |
| `uninstall-dev.ps1` | 卸载开发版并清理注册；默认保留用户数据 |
| `repair-registration.ps1` | 修复当前用户 TSF/COM 注册 |
| `verify-windows.ps1` | 检查 Windows 开发安装和运行时状态 |
| `run-tests.ps1` / `run-ime-tests.ps1` | 运行开发回归或输入法测试 |
| `Run-Portable-Tests.cmd` | 运行便携包测试 |
| `set-schema.ps1/.cmd` | 切换全拼/双拼方案 |
| `set-candidate-page-size.ps1/.cmd` | 调整候选页大小 |
| `start-preview.cmd` | 启动独立预览程序 |
| `import-dicts.ps1` / `update-dictionaries.cmd` | 导入或更新开发词库 |
| `test-real-world-corpus.cmd` | 运行真实语料回归 |
| `build.sh` | 非 Windows 开发构建入口 |
| `release.cmd` | 调用 Windows 发布流程的兼容入口 |

## 根目录兼容脚本

- 构建：`build_windows_vs2026.ps1/.cmd`、`build_linux.sh`。
- 词库：`build-dictionaries.ps1`、`update-dictionaries.ps1`、`query-dictionary.ps1`、`convert-english-wordfreq.ps1`。
- 测试：`run-portable-tests.ps1`、`test-real-world-corpus.ps1`、`generate-human-input-scenario.ps1`。
- 兼容发布：`package_release.ps1`。
- 公共命令封装：`native-command.ps1`，统一捕获原生命令输出和退出码。

根目录脚本仍被 CMake、CI、文档或旧命令调用，暂不强行移动；新工作统一从 `workflows` 入口进入。

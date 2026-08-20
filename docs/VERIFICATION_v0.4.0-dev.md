# PiInput v0.4.0-dev 验证记录

验证环境：Windows x64，Visual Studio 18 2026 Build Tools，MSVC 19.51，Windows SDK 10.0.26100.0。

## Release 构建

- `Visual Studio 18 2026` x64 Release 全目标构建：通过；
- `PiInputTSF.dll`：通过；
- `PiInputHost.exe`：通过；
- `PiInput-Install.exe`：通过；
- `PiInput-Uninstall.exe`：通过；
- `piinput-diagnostics.exe`：通过；
- 命令行工具、测试台与所有测试目标：通过。

## 完整自动测试

```text
49/49 tests passed
0 tests failed
Total Test time: 112.34 seconds
```

覆盖范围包括：

- 407 个全拼合法音节、406 个用户确认的小鹤合法码；
- 3500/7000 汉字全拼和小鹤检索覆盖；
- 未完成音节与任意长度增量候选；
- 长句、段落、诗词、专业词汇、分段取字和候选稳定性；
- 中文/英文标点、完整内置符号及 `;sheshidu → ℃`；
- 英文离线前缀候选、排序、学习与词库转换；
- 候选默认一行，只有正式 `=`/`↓` 展开；
- 有界 IPC、Host 会话、管道 ACL、非阻塞客户端与组合状态镜像；
- 真实 Host 进程 `wo → 我`；
- Host 连续 20/20 次停止、重启、恢复 `w` 后继续 `o → wo → 我`；
- 稳定运行目录、安装事务、迁移、失败回滚、原生卸载；
- 发布元数据、品牌、PowerShell 入口、源文件与 SHA-256 完整性；
- 外部约 50 万条词库、真实 SCEL 与增量性能门禁。

## 系统状态

开发与自动测试期间没有安装到 Windows 输入法列表。验证开始前已确认旧 PiInput 运行目录、旧 Profile/COM 注册和已加载旧 DLL 均已清理；本版最终 ZIP 只生成到本地 `artifacts`，由用户手动安装测试。

## 架构验证

- `PiInputTSF.dll` 不链接 `piinput_core`，不加载词库，不拥有候选窗口；
- 永久入口只包含 COM/TSF、组合镜像和 `PIINPUT_IPC_V1` 客户端；
- `PiInputHost.exe` 在接受连接前加载不可变词库快照；
- Space/数字选词由当前 Host 快照解析，避免过期候选；
- 每个正式按键携带最新确认的 raw/caret/mode，Host 重启后重新计算候选；
- 安装器普通升级不覆盖稳定 Shim，只原子切换并健康检查版本化 Host；
- 诊断工具通过 JSON 报告永久 Shim、SHA-256、当前 Host、协议与 Profile 状态。

## 尚需用户手动验证

自动测试不能代替真实 Windows 应用兼容性。用户安装后应在记事本、Notepad4、ChatGPT、Chrome、VS Code 中验证：切换输入法、中文/英文、候选窗位置、Shift、`=`/`-`、数字选词、符号搜索，以及在应用保持打开时再次运行安装器升级 Host。

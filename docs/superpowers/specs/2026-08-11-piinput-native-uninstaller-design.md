# PiInput 正式卸载器设计

日期：2026-08-11  
状态：等待用户确认书面设计

## 1. 目标

PiInput 安装包必须同时提供可发现、可重复执行、失败时不破坏现有输入环境的正式卸载能力。用户可以从 Windows“已安装的应用”或安装目录运行卸载器；卸载后可以直接重新安装新版。

## 2. 方案比较

### 方案 A：原生 `PiInput-Uninstall.exe`（采用）

与现有 C++ 安装器共享安全路径、注册信息和错误处理模块。安装时把卸载器复制到稳定目录，并写入 Windows 当前用户卸载项。优点是无 PowerShell 策略和编码依赖、界面统一、可精确处理 TSF；代价是需要实现两阶段自删除。

### 方案 B：发布 PowerShell 卸载脚本

可以复用现有 `uninstall-dev.ps1`，开发量较小，但可能受到执行策略、编码、控制台窗口和用户误改脚本影响，不适合作为正式用户入口。

### 方案 C：引入 MSI、WiX 或 Inno Setup

具备成熟的安装维护界面，但会新增较大的工具链和安装模型迁移成本。当前开发版仍采用按用户、并行版本目录，现阶段引入不划算。

## 3. 安装后的入口

安装器创建：

```text
%LOCALAPPDATA%\PiInput\Uninstall\PiInput-Uninstall.exe
```

并在当前用户注册：

```text
HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\PiInput
```

至少写入 `DisplayName`、`DisplayVersion`、`Publisher`、`InstallLocation`、`DisplayIcon`、`UninstallString`、`QuietUninstallString`、`NoModify` 和 `NoRepair`。因此 Windows“设置 → 应用 → 已安装的应用”能够显示并启动卸载。

发布 ZIP 根目录也附带 `PiInput-Uninstall.exe`。该副本负责定位当前活动安装；真正执行删除前，它会复制到临时目录并启动工作进程，避免删除自身失败。

## 4. 用户界面

普通运行时显示确认窗口：

- 明确说明将移除 PiInput 输入法和程序文件；
- 提供“同时删除用户词库、设置和学习记录”复选框；
- 复选框默认不勾选；
- “取消”不改变任何系统状态；
- 完成窗口说明是否有文件安排在下次登录或重启时删除。

静默接口：

```text
PiInput-Uninstall.exe --silent
PiInput-Uninstall.exe --silent --remove-user-data
```

静默模式不显示窗口，但使用相同安全流程和退出代码。

## 5. 卸载流程

1. 通过 `current.txt` 和安全路径校验解析活动版本，拒绝操作 `%LOCALAPPDATA%\PiInput` 以外的路径。
2. 调用活动版本的 `piinput-profile.exe --disable-user`，从当前用户键盘列表移除 PiInput。
3. 调用 `--deactivate`，尽力停用当前 Profile。
4. 调用 `--unregister` 并执行 DLL 注销；关键注销失败时停止，不删除运行时，保留卸载入口以便重试。
5. 删除 `Dev` 下的当前标记和版本目录；被应用占用的 DLL 或目录通过 `MoveFileEx(..., MOVEFILE_DELAY_UNTIL_REBOOT)` 安排延迟删除，不强制终止任何用户程序。
6. 删除开始菜单入口和 Windows 卸载项。
7. 默认保留 `%LOCALAPPDATA%\PiInput\UserData`。
8. 仅当用户勾选或传入 `--remove-user-data` 时删除 `UserData`。
9. 当 `PiInput` 根目录为空时删除根目录；否则保留用户数据目录。
10. 临时工作进程安排自身删除并返回明确退出代码。

## 6. 重新安装与升级

卸载不要求强杀正在使用 TSF DLL 的应用。若旧文件仍被锁定，它们留在旧的版本化目录并安排延迟删除；新安装继续创建新的版本目录，并把 COM/TSF 注册指向新 DLL，因此用户关闭并重新打开目标应用后即可测试新版。

安装器每次成功安装时刷新卸载器副本和卸载注册项。升级失败时不能覆盖或删除已有的有效卸载入口。

## 7. 错误处理与安全约束

- 不使用通配符删除安装目录。
- 所有递归删除目标必须经过绝对路径校验并位于 `%LOCALAPPDATA%\PiInput` 内。
- 不强制结束记事本、浏览器、Codex、Visual Studio 或其他加载 TSF DLL 的进程。
- Profile 或 DLL 注销失败时采用 fail-closed：不删除运行时、不伪报成功。
- “删除用户数据”必须由显式复选框或命令行参数授权。
- 重复卸载应返回“PiInput 已经卸载”，而不是报错或误删其他目录。

## 8. 测试与发布门槛

自动测试至少覆盖：

- 卸载注册项内容和引用路径；
- 根目录与安装目录两个卸载器入口；
- 默认保留用户数据；
- 显式删除用户数据；
- 取消操作零副作用；
- 无当前标记、损坏标记和越界路径均拒绝删除；
- Profile/DLL 注销失败时保留运行时；
- 文件未锁定时完整删除；
- 文件锁定时登记延迟删除且不杀进程；
- 重复卸载幂等；
- 卸载后再次安装成功；
- 发布 ZIP 包含安装器、卸载器、测试程序和安装使用文档。

发布前必须完成 Release 全目标构建、全量 CTest、最终 ZIP 内容检查以及安装/卸载命令的隔离目录集成测试。本轮仍不自动修改用户当前系统安装，由用户自行运行最终安装包测试。

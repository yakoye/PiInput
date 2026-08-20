# PiInput v0.7.0 验证记录

## 修复依据

- 标点后停顿的真机追踪显示：TSF 编辑同步完成，Host 计算为 0 毫秒，停顿来自无回复 `commit_result` 被重复发送直到 750 毫秒重试窗口结束。
- 数字规则检查显示：候选数字键定义为 `1` 到 `9`，直通数字上下文错误复用了同一判断，因此漏掉 `0`。
- 开机黑窗口对应的 `HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\PiInputHost` 直接指向控制台程序；安装器和 Shim 已经具备 `CREATE_NO_WINDOW | DETACHED_PROCESS` 后台启动路径，因此登录启动项没有必要。

## 自动验证

- Windows x64 Release 全目标构建：通过；
- 完整 CTest：59/59 通过，0 失败；
- `piinput-pipe-client`、`piinput-host-process`、`piinput-host-restart-handoff`：通过；
- 三个长文本真实协议回放 `piinput-typing-latency`：通过；
- `piinput-keystroke-latency`：通过；
- 发布版本、文件清单与源码 SHA-256 门禁：通过；
- `dist/windows-x64` 已由当前 Release 构建重新生成，核心二进制与构建目录哈希一致。

长文本性能门禁在紧接并行全目标构建的一次运行中受系统负载影响越界；静置后单项复跑通过，随后再次执行完整 59 项 CTest 全部通过。最终发布依据是最后一次完整全绿结果。

## 用户真机验证

- `我们。我们`、`。我们`：标点后首键立即显示；
- `0.7.0`：保持 ASCII 点；
- `0..`：输出 `0.。`；
- 安装后关闭并重新打开测试应用，确保加载 v0.7.0 Shim。
- 重新登录 Windows，确认不再出现 `PiInputHost.exe` 黑窗口；首次使用输入法仍能自动拉起后台 Host。

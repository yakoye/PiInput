# PiInput v0.7.16 验证记录

验证日期：2026-08-26

<!-- release-gates
host_soak_8h=NOT_RUN
tsf_app_soak_8h=NOT_RUN
p0_real_host_matrix=NOT_RUN
installer_ui_real_run=NOT_RUN
-->

## 自动验证

- 冻结提交 `4a4b3660`，build ID 为 `0.7.16+4a4b366`（以候选包内实际 `--build-id` 为准）。
- 干净 Release 构建完成 67/67 CTest 通过，含本版新增的候选窗遮挡避让单测与卸载器延迟删除源码门禁。
- `piinput-keystroke-latency` 与 `piinput-host-process` 通过，确认识别搜索面板宿主所需的 `OpenProcess` 查询没有落在按键路径上（结果按 owner 窗口缓存，owner 变化时才解析一次）。

## 实机验证（已完成）

- **Windows 搜索候选可见**：Windows 11 家庭版中文版 Build 26200（25H2）、125% 缩放，在任务栏搜索框输入小鹤双拼 `vswf`，候选行完整可见并可翻页。验证前的同一构建在同一环境下候选完全不可见。
- **遮挡诊断证据**：修复前的窗口追踪显示候选窗 `IsWindowVisible=True`、`WS_EX_TOPMOST|TOOLWINDOW|NOACTIVATE`、坐标精确锚定在光标下方 5px，与记事本下可见时的窗口状态完全一致，唯一差别是前台宿主。据此排除"候选未绘制"，确认为搜索面板层级遮挡。
- **UIElement 协议实测**：`piinput-candidate-trace.on` 采集到 `BeginUIElement.hr=0`、`BeginUIElement.show=1`，且全程无 `QI.integratable`。确认 Windows 11 25H2 的搜索框既未要求接管候选显示，也未查询搜索框集成接口。
- **普通宿主无回归**：记事本下候选窗位置与修复前一致；单测 `test_a_host_that_hides_top_most_windows_pushes_the_bar_clear` 断言未重叠遮挡矩形时的落点与基准逐像素相同。

## 尚未验证（阻止转为正式版）

- **安装器新界面未实机运行**：确认页、进度页、完成页均未在真实安装流程中走过。`--silent` 路径的行为不变已由代码路径保证，但交互安装需实跑。
- **一键更新原地升级未实机运行**：UAC 由两次降为最多一次的实际效果、`-CleanReinstall` 分支均未实跑。
- **重启后文件保留未验证**：本版修复的核心缺陷需要"升级 → 重启 → 确认文件仍在"的完整闭环才能确认，目前只有代码修复与源码门禁。
- Host 8 小时、TSF/App 8 小时、P0 真实宿主矩阵均未运行。
- 无可信 Authenticode 签名与 RFC 3161 时间戳。
- 未验证 100% / 150% / 200% 缩放下搜索面板避让的视觉效果。间距由 DIP 定义并随 DPI 同比缩放（`gap` 与 `kObstructionSink` 均为 DIP），理论上视觉偏移恒定，但只在 125% 下实测过。

## 结论

本版修复了两个用户可见的严重问题，自动回归与关键实机现象均已确认，但安装、升级、重启闭环和签名门禁未完成。**只能作为候选版分发，不得转入 `releases/current`。**

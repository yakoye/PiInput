# PiInput v0.3.3-dev 验证记录

## 根因证据

- `v0.3.2-dev` 的 `TextService::Activate` 直接调用完整 `load_engine()`；
- 完整二进制词库包含 459,505 条词条；
- Windows 真机基准的单进程词库加载耗时约 690ms；
- TSF DLL 会由多个桌面宿主加载，因此激活阶段的按进程同步展开会放大资源占用；
- Windows 事件日志未发现对应 `0xc00000fd` 崩溃记录，所以本记录将现象归类为高开销初始化导致的系统卡顿风险，不虚构未得到证据支持的异常码。

## TDD 证据

修复前已确认以下门禁失败：

- CandidateGrid 仍从配置的三行开始显示；
- `LazyLoadGate` 接口不存在；
- Windows 源码门禁发现 `Activate` 仍调用 `load_engine()`。

最小实现后，三项定向 Release 测试全部通过：

- `piinput-candidate-grid`；
- `piinput-lazy-load-gate`；
- `piinput-windows-source-regression`。

## 最终验证

2026-08-11 在 Windows 11、Visual Studio 18 2026、MSVC 19.51、Windows SDK 10.0.26100.0 环境完成：

- 从空的 `build/windows-x64` 目录重新配置；
- Windows Release 全目标构建成功；
- `PiInputTSF.dll`、`PiInput-Install.exe` 和全部工具均成功链接；
- CTest `24/24` 通过，`0` 失败；
- 外部 459,505 条词库增量性能门禁通过；
- 外部完整词库候选回归通过；
- 安装布局包含 `data/piinput-base.lex`；
- `git diff --check` 无空白错误。

交付包：

```text
C:\Users\color\Downloads\PiInput\PiInput-repo\artifacts\PiInput-v0.3.3-dev-windows-x64.zip
SHA-256: 27f1ff57e6308f5ad76761563cd6016fee553e539bf0a94b2e2a4dca309405a2
```

构建目录、暂存目录和交付包中的 `PiInputTSF.dll` SHA-256 完全一致。已撤回的 v0.3.2 包被移到 `C:\Users\color\Downloads\PiInput\withdrawn\v0.3.2-unsafe`，避免误装。

自动安装启动了两次，但两次 UAC 都由 Windows 返回“操作已被用户取消”，因此本轮没有谎报已安装；当前有问题的旧 Profile 继续保持当前用户禁用状态。用户可从已解压交付目录双击 `PiInput-Install.exe`，并在 UAC 中选择“是”。

完整测试包括核心输入、设置、候选网格、禁止重入延迟加载、全拼变体、增量解码、英文候选、用户键盘注册、安装布局、迁移、品牌、SHA-256、发布元数据、脚本、语料和性能门禁。

安装后仍需由用户在真实目标应用中验证：

1. 仅切换到 PiInput 不弹候选窗、不持续卡顿；
2. 第一个字母触发一次初始化后正常候选；
3. 候选首次只显示一行；
4. `=`/`↓` 才展开并下移，`-`/`↑` 才展开并上移；
5. 新增输入后重新折叠为一行。

真机用户验收结果不会在实际完成前写成“已通过”。

# PiInput v0.4.0-dev 版本说明

## 本版核心变化

本版解决旧开发架构中“不同应用长期加载不同版本 DLL、升级必须逐个关闭程序”的根本问题。

- 新增永久稳定、轻量的 `PiInputTSF.dll`，只负责 Windows TSF 编辑、组合状态镜像和本地 IPC；
- 新增独立 `PiInputHost.exe`，统一持有全拼、小鹤、词库、排序、英文、符号、设置和候选窗口；
- 普通升级只安装并切换版本化 Host，不再覆盖应用进程已经加载的稳定 Shim；
- Host 断线或升级后，下一次按键携带已确认组合串、光标和中英文状态并恢复候选；
- 安装器在切换前排空旧 Host，切换后执行健康检查，失败时回滚旧 Host；
- 永久 Profile 使用固定 CLSID/Profile GUID，避免每版出现一个 Windows 键盘项；
- 新增 `piinput-diagnostics.exe`，报告 Profile、Shim 路径与 SHA-256、当前 Host 与实时健康状态；
- 符号搜索迁移到 Host，继续支持 `;sheshidu → ℃`；
- Space 和数字选词由 Host 根据当前候选快照解析，避免快速输入时使用过期 Shim 候选；
- 候选窗默认严格保持一行，只有正式按下 `=`/`↓` 才展开；
- 新增 Host 20 次连续重启/恢复集成测试。

## 安装布局

```text
%LOCALAPPDATA%\PiInput\Runtime\Shim\PiInputTSF.dll
%LOCALAPPDATA%\PiInput\Runtime\versions\<版本>\bin\PiInputHost.exe
%LOCALAPPDATA%\PiInput\Runtime\current.json
%LOCALAPPDATA%\PiInput\UserData
```

## 兼容与限制

- 当前为 Windows x64 开发测试版，尚未正式签名；
- 第一次从 v0.3.x 迁移到本版会注册新的永久 Profile；以后普通 Host 升级不需要关闭应用；
- 只有未来确实需要修改永久 Shim 本身时，发布说明才会明确要求注销或重启；
- 本版仍保持纯离线、无 AI、无语音、无广告、无输入内容上传。

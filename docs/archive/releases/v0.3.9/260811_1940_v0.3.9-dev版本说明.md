# PiInput v0.3.9-dev 版本说明

本版继续收敛现有输入体验，不增加 AI、语音或云端实时联想。

## 英文候选

- 继续使用 24,323 词的固定离线英文词频库；
- 原始输入固定为第一候选；
- 新增按“当前已输入前缀”生效的独立偏好排序，不再用一个全局高权重污染所有短前缀；
- 严格回归首屏顺序：
  - `r → r、right、really`；
  - `re → re、really、remember`；
  - `rev → rev、review、reverse`；
  - `reve → reve、revile、reverse`；
  - `b → b、but、because`；
  - `bo → bo、both`；
  - `boo → boo、book、boom`；
  - `book → book、books、booked`；
- 其他英文候选继续由用户词、用户学习、离线词频、补全长度和稳定 ID 确定性排序。

## 安装与卸载

- 新增原生 `PiInput-Uninstall.exe`；
- 安装器将卸载器注册到 Windows“已安装的应用”；
- 默认保留用户词库、设置和学习记录；
- 只有用户主动勾选时才删除用户数据；
- 不强制结束加载了 TSF DLL 的应用，锁定文件安排在重启后清理；
- TSF Profile 改用 `PiInputTSF.dll` 内嵌的紫色 `π` 图标，避免 Windows 回退显示“简体”方块。

## 工具与发布

- 保留 v0.3.8 对 `query-dictionary.cmd` 的 CRLF 修复；
- 修复 Windows PowerShell 将“不支持的 SCEL”标准错误误判为整个词库更新失败的问题；
- 发布包根目录同时提供安装器、卸载器和独立输入测试台；
- 安装与使用方法统一记录在 `安装与使用指南.md`。

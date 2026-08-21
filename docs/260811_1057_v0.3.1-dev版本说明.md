# PiInput v0.3.1-dev 版本说明

本版只修复 Windows 首次安装可见性，不扩展输入功能。

## 修复

- 安装器在注册 TSF Profile 后，显式调用 Windows `InstallLayoutOrTip`，把 PiInput 加入当前用户的键盘列表。
- 安装顺序固定为：用户键盘列表注册、Profile 激活、状态检查。
- 任一步骤失败都会返回安装失败，不再用 `registered/enabled/active` 代替 `Win + Space` 可见性。
- 卸载和修复脚本同步增加当前用户键盘列表的移除或恢复。
- `piinput-profile.exe` 新增 `--enable-user` 和 `--disable-user` 诊断命令。

## 未改变

- 默认小鹤双拼；
- 全拼、增量候选、候选排序和词库格式；
- 无 AI、无语音、无云端实时联想；
- 用户词库和设置继续保存在 `%LOCALAPPDATA%\PiInput\UserData`。

# Windows TSF 开发测试指南

## 一键构建与安装

在源码根目录双击 `setup-dev.cmd`。脚本构建 Release、运行全部测试、准备 `dist/windows-x64`，最后启动 `PiInput-Install.exe --silent`。

也可以在构建完成后直接双击：

```text
dist\windows-x64\bin\PiInput-Install.exe
```

开发安装器采用版本并存：

```text
%LOCALAPPDATA%\PiInput\Dev
├── current.txt
└── versions
    └── 0.2.0-<UTC时间-PID>
        ├── bin
        │   ├── PiInputTSF.dll
        │   └── piinput-profile.exe
        └── data
```

它不会覆盖正在被应用占用的旧 DLL，不会强制关闭记事本、Notepad++、Explorer 等应用。安装完成后重新打开目标应用即可加载新版；旧版本目录在不再占用时由后续安装清理。用户词库、学习和设置始终保存在 `%LOCALAPPDATA%\PiInput\UserData`。

## 自动检查

```powershell
.\verify-windows.ps1
```

检查构建 DLL 与注册 DLL、COM 路径、词库及 TSF profile。当前版本目录可查看：

```powershell
Get-Content "$env:LOCALAPPDATA\PiInput\Dev\current.txt"
```

## 输入验收

重新打开记事本，使用 `Win+Space` 选择“PiInput 中文输入法（开发版）”。默认小鹤双拼。

```text
jisrji + Space → 计算机
gjjt   + Space → 感觉
mkt             → 明天等即时候选
rug             → 如果、入股等即时候选
```

全拼模式执行 `set-schema.cmd full`，重新打开目标应用后测试：

```text
jisuanji → 计算机
mingt    → 明天等即时候选
```

## 当前按键

```text
A-Z             输入拼音或双拼编码
'               组合状态下手动拼音边界
Backspace/Delete 删除光标前/后字符
Left/Right      移动输入光标
Home/End        移到首尾
Up/Down         移动候选
- / =           上一页 / 下一页
PageUp/PageDown 上一页 / 下一页
1~9             选择当前页候选
Space           上屏当前候选
Enter           上屏原始字母
Esc             取消输入
单独 Shift      切换中文/英文
```

中文空闲状态下直接标点会转换，例如 `,→，`、`.→。`、`;→；`、`?→？`、`<→《`。英文模式输出 ASCII。旧的 `;sheshidu` TSF 快速符号入口已经让位于中文分号；符号检索核心仍保留，新的可配置触发键尚未实现。

## 故障信息

发生问题时提供 `setup-dev.cmd` 完整输出、`verify-windows.ps1` 输出、目标应用名、Composition/候选/上屏分别进行到哪一步，以及 `%LOCALAPPDATA%\PiInput\Dev\current.txt` 内容。

当前限制：仅 x64、开发版未签名、候选窗仍为 GDI、尚无设置 GUI、密码框和全面应用兼容性尚未完成。

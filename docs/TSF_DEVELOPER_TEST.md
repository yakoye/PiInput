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

离线英文候选默认关闭，因此切换到英文后仍由系统直接处理字母和标点。需要测试时，在
`%LOCALAPPDATA%\PiInput\UserData\settings.ini` 增加：

```ini
[english]
enabled=true
builtin_dictionary=true
user_dictionary=true
user_learning=true
items_per_row=6
```

开启后，英文模式输入 ASCII 字母会启动 Composition，并从安装目录
`data/english_lexicon.tsv`、可选下载的 `english_downloaded.tsv` 与用户维护的
`english_user.tsv` 读取本地候选；选择学习保存在 `english_learning.tsv`。英文候选不
联网。Space 或数字选择候选，Enter 提交原始输入，Composition 中输入标点会先提交当前
候选（无候选则提交原始输入）再提交 ASCII 标点。

用户英文词典路径是
`%LOCALAPPDATA%\PiInput\UserData\english_user.tsv`，每行格式为
`word<TAB>positive_weight`，也兼容可选第三列
`word<TAB>positive_weight<TAB>flags`。`word` 必须是 ASCII-only 的 `A-Z`/`a-z`，
`positive_weight` 必须是正整数，非法行会被忽略。完全相同大小写的重复词合并为一项：
保留首次稳定 ID、采用最大权重，用户词优先于内置词，用户行的非空 `flags` 替换内置
flags；大小写不同的词保留为不同候选，但前缀匹配不区分大小写。

- `builtin_dictionary=false`：不加载安装目录的内置英文 TSV，也不加载
  `%LOCALAPPDATA%\PiInput\UserData\english_downloaded.tsv`；
- `user_dictionary=false`：不加载 `english_user.tsv`；
- `user_learning=false`：不加载、不记录也不保存英文选择学习；
- `enabled=false`：英文模式保持系统直通，不加载或查询任何英文词库。

`%LOCALAPPDATA%\PiInput\UserData\english_learning.tsv` 由程序管理并原子写入，请勿作为
用户词典手工编辑；需要自定义单词时只编辑 `english_user.tsv`。
`english_downloaded.tsv` 同样由词典更新脚本管理；它与 `english_user.tsv` 分离，更新
第三方英文源不会覆盖用户维护的词典。

## 故障信息

发生问题时提供 `setup-dev.cmd` 完整输出、`verify-windows.ps1` 输出、目标应用名、Composition/候选/上屏分别进行到哪一步，以及 `%LOCALAPPDATA%\PiInput\Dev\current.txt` 内容。

当前限制：仅 x64、开发版未签名、候选窗仍为 GDI、尚无设置 GUI、密码框和全面应用兼容性尚未完成。

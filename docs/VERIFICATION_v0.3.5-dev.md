# PiInput v0.3.5-dev 验证记录

本文件记录发布前必须重新执行的验证。最终结果只在全新 Release 构建和完整 CTest 完成后填写。

## 固定门禁

- Windows x64 Release 全目标构建；
- 完整 CTest；
- 406 个用户确认的小鹤码解析；
- 3500/7000 汉字全拼和小鹤检索覆盖；
- 全部内置符号与键盘标点；
- 增量候选、长句、专业词、英文候选和排序稳定性；
- 外部大词库延迟门禁；
- 安装布局、迁移、注册、失败回滚和品牌门禁；
- 发布 ZIP 内容和 SHA-256；
- 安装器不自动激活输入法；
- 新启动的非 PiInput 测试进程不加载已注销的旧 TSF DLL。

## 已确认的词库覆盖

用户提供的 `.xls` 仅作为本地测试输入，不进入源码包或用户安装包。转换后的纯字符清单位于外部 `dicts/tests`：

```text
3500常用汉字.txt  SHA-256 0A1A3B6A0ACABA00AC0688F8E81C9D9317296AE1B15D826C24B58201648627F1
7000通用汉字.txt  SHA-256 82B0330C952E1266615A6CFD6EC13D2A6B7F23B4A3B7C0BC3C659AB28CF9B62C
```

```text
common_full=3500/3500
common_xiaohe=3500/3500
general_full=7000/7000
general_xiaohe=7000/7000
```

## 最终 Release 结果

2026-08-11 在 Windows x64、MSVC 19.51、Windows SDK 10.0.26100.0 上执行：

```text
Release 全目标构建：通过
CTest：25/25 通过，0 失败
3500 常用汉字：全拼 3500/3500，小鹤 3500/3500
7000 通用汉字：全拼 7000/7000，小鹤 7000/7000
外部大词库增量性能：通过
完整语料、符号、标点、英文候选、安装与迁移门禁：通过
PiInput-Test.exe 独立启动：通过
PiInput-Test.exe 加载 PiInputTSF.dll 数量：0
首次状态可见性有限重试后的静默安装：exit 0
安装后注册状态：registered=yes, enabled=yes
安装后当前运行目录：0.3.5-20260811-062903-34688
已安装 DLL 与 Release DLL SHA-256：一致
```

最终系统安装前再次确认旧 CLSID、Profile 和用户键盘列表项均已注销；安装器仅启用用户键盘列表，不自动激活 PiInput。真实输入验证时才显式激活 Profile，新启动的 Notepad4 加载 `0.3.5` DLL 并显示候选窗。仍被旧应用占用的 0.3.0～0.3.4 文件已登记为重启后删除。

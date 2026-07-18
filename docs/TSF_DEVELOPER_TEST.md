# Windows TSF 开发测试指南

## 1. 适用版本

本文档适用于 `v0.1.6-dev` 的 TSF 开发注册与输入基线。

## 2. 构建和注册

在源码根目录：

```powershell
.\setup-dev.cmd
```

默认方案是小鹤双拼。全拼：

```powershell
.\setup-dev.ps1 -Schema full
```

完整流程应生成：

```text
dist\windows-x64\bin\LiteImeTSF.dll
dist\windows-x64\bin\liteime-profile.exe
```

并安装到：

```text
%LOCALAPPDATA%\LiteIME\Dev
```

## 3. 自动检查

```powershell
.\verify-windows.ps1
```

自动检查包括：

- 内置基础词库存在；
- 全拼 `jisuanji` 返回“计算机”；
- 小鹤 `jisrji` 返回“计算机”；
- COM 注册键存在；
- 注册 DLL 路径可显示；
- TSF profile 输出 `registered=yes`；
- TSF profile 输出 `enabled=yes`。

自动检查不能代替真实 TSF 输入测试。

## 4. 记事本最小测试

1. 关闭所有旧记事本窗口；
2. 新开记事本；
3. 按 `Win+Space`；
4. 选择“LiteIME 中文输入法（开发版）”；
5. 默认小鹤输入 `jisrji`；
6. 检查是否出现 `jisrji` Composition；
7. 检查是否出现候选“计算机”；
8. 按空格；
9. 检查文本框中是否写入“计算机”。

## 5. 当前按键

```text
A-Z             输入拼音或双拼编码
'               手动拼音边界
Backspace       删除光标前字符
Delete          删除光标后字符
Left/Right      移动输入光标
Home/End        移到首尾
Up/Down         移动候选
PageUp/PageDown 候选翻页
1~9             选择当前页候选
Space           上屏当前候选
Enter           上屏原始字母
Esc             取消输入
;               空输入状态下进入符号搜索
```

符号示例：

```text
;sheshidu + Space -> ℃
```

## 6. 切换方案

```powershell
.\set-schema.cmd full
.\set-schema.cmd flypy
.\set-schema.cmd natural
.\set-schema.cmd mspy
.\set-schema.cmd abc
```

切换后需要在目标应用中重新选择一次 LiteIME，或关闭后重新打开应用。

## 7. 注册修复

```powershell
.\repair-registration.ps1
```

修复脚本会依次执行：

```text
允许旧 profile 不存在或未激活
→ 注销旧 DLL
→ 注册新 DLL
→ 显式注册并启用 profile
→ 激活 profile
→ 输出 registered/enabled/active 状态
→ 刷新 ctfmon.exe
```

单独查看状态：

```powershell
& "$env:LOCALAPPDATA\LiteIME\Dev\bin\liteime-profile.exe" --status
```

然后：

1. 完全关闭目标应用；
2. 重新打开；
3. 再使用 `Win+Space` 选择 LiteIME。

## 8. 卸载

保留用户数据：

```powershell
.\uninstall-dev.ps1
```

同时删除用户数据：

```powershell
.\uninstall-dev.ps1 -RemoveUserData
```

## 9. 提交故障日志

发生问题时提供：

- `setup-dev.cmd` 从 `Using CMake:` 开始的全部输出；
- `verify-windows.ps1` 全部输出；
- 问题发生在哪个应用；
- 是否能在 Win+Space 中看到 LiteIME；
- Composition、候选窗、上屏分别进行到了哪一步；
- 截图；
- Windows 版本与应用位数。

## 10. 当前限制

- 仅 x64；
- 候选窗是临时 GDI UI；
- 候选定位使用系统 caret 的保底方式；
- 未完成中英文切换；
- 未接入标点状态机；
- 未完成密码框和敏感输入处理；
- 未完成多应用兼容测试；
- 未签名，不用于公开分发。

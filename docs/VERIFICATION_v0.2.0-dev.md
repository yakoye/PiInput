# LiteIME v0.2.0-dev 验证记录

## 环境

- Windows 11 SDK 10.0.26100.0
- Visual Studio 18 2026 / MSVC 19.51
- Release x64
- 本地基础词库：459,505 条

## 自动测试门禁

干净 Release 构建执行以下 CTest：

```text
liteime-core-tests
liteime-installer-layout-tests
liteime-windows-source-regression
liteime-dictionary-script-regression
liteime-corpus-regression
liteime-performance-smoke
liteime-external-dictionary-regression
liteime-scel-regression
```

语料包 SHA-256：

```text
91435EC216F805F8654DBA703FB41734B8B4292D5D2CFBFC95DEA838EA588DA8
```

结构检查：407 个标准全拼音节、451 个唯一结构化测试 ID、786 条结构化用例。

## 真实词库候选

```text
mkt →
1. 明天
2. 明天上午
3. 命题逻辑
4. 命题演算
5. 命题
6. 名堂

rug →
1. 如果
2. 入股
3. 乳沟
4. 如歌
5. 如故
6. 入关
```

其他既有门禁包括 `gjjt→感觉`、`xmzd→现在`、`jisrji→计算机`、`接触`、`词汇`、单字 9 项、词语 6 项、翻页和 Shift 状态。

459,505 条词库对 `mkt` 执行 5,000 次核心查询的样本：

```text
average_us=31.570
p50_us=28.5
p95_us=43.7
p99_us=68.0
```

这是核心查询，不等同于 TSF 窗口绘制端到端延迟。

## 安装器集成验证

安装时，ChatGPT 与 Explorer 仍加载旧固定路径，Chrome、VS Code、Notepad++、Edge、微信等进程仍加载前一个版本目录：

```text
%LOCALAPPDATA%\LiteIME\Dev\bin\LiteImeTSF.dll
```

在不关闭这些进程的情况下运行完整 `setup-dev.ps1 -NoClean -SkipDictionaryImport`，退出码为 0，注册路径切换为：

```text
%LOCALAPPDATA%\LiteIME\Dev\versions\0.2.0-20260718-234159-17612\bin\LiteImeTSF.dll
```

状态检查：

```text
registered=yes
enabled=yes
active=yes
flags=0x3
```

这证明开发安装器无需覆盖占用中的旧 DLL，也无需强制关闭用户应用。应用只有在重新打开后才会加载新 DLL，这是 Windows 进程内 COM/TSF DLL 的正常行为。

## 尚需人工验收

- 在重新打开的记事本、Notepad++、浏览器中检查中文标点实际上屏；
- 检查 `mkt`、`rug` 的候选窗口视觉与选词；
- 检查多显示器、高 DPI 和更多应用兼容性；
- 正式签名安装包不属于本开发版验证范围。

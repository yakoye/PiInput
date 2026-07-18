# v0.2.0 候选交互与常用词验证记录

## 环境

- Windows 11 10.0.26200
- Visual Studio 18 2026 / MSVC 19.51
- Windows SDK 10.0.26100.0
- Release x64

## 自动验证

- CMake Release 全目标编译：通过；
- `LiteImeTSF.dll`、`liteime-core-tests.exe` 等全部目标链接：通过；
- CTest：6/6 通过；
- 外部词库：459,505 条；
- `wo` 10,000 次查询：P95 10.4 μs，P99 15.3 μs；
- 真实文本语料：67 个片段，Top 10 命中 41 个，基线 61.19%。

## 关键候选

```text
jpiu   → 接触（第 1）
jiechu → 接触（第 1）
cihv   → 词汇（第 1）
cihui  → 词汇（第 1）
gjjt   → 感觉（第 1）
```

长句：

```text
我今天下午要去超市买点水果（第 2）
固件开发需要熟悉底层寄存器配置和链路状态机（第 1）
这是一个关键的测试还是一个关于测试的决定（第 1）
```

## 尚需人工验证

安装当前 DLL 后，在记事本中确认：横向候选、9/6 项分页、`-`/`=`、数字键选词和单独 Shift 中英文切换。`verify-windows.ps1` 必须先确认已注册 DLL 哈希与本次构建一致。

当前旧 DLL 被 ChatGPT、Chrome、Explorer、Notepad++ 和 PixPin 加载，自动验证期间未强制关闭这些程序。关闭占用程序后双击 `refresh-installed-dev.cmd`，即可只替换当前构建而保留已经成功的 TSF 注册。

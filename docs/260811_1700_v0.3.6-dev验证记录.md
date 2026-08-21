# PiInput v0.3.6-dev 验证记录

验证日期：2026-08-11  
平台：Windows 11 x64、Visual Studio 18 2026 Build Tools、Windows SDK 10.0.26100.0

## Release 构建

- CMake Release 全目标构建成功；
- `PiInputTSF.dll`、`PiInput-Install.exe`、`PiInput-Test.exe` 及全部词库工具均生成；
- `piinput_icon.ico` 已安装到发布目录，并由 TSF Profile 注册使用；
- 最终发布包内含 501,935 条词条的 `piinput-base.lex`。

## 自动测试

完整 CTest：

```text
31/31 tests passed
0 tests failed
```

覆盖范围包括：

- 全拼、小鹤双拼、合法编码、`u/v` 兼容及未完成编码；
- 单字覆盖、长句切分、增量候选和候选稳定性；
- 候选横排、折叠/展开、分段取字、撤销及陈旧候选失效；
- 中文标点、符号、英文离线候选和用户学习；
- 词库构建、SCEL、THUOCL、词库反查及本地更新脚本；
- TSF 延迟加载、安装目录、迁移、卸载和 Windows 注册源码约束；
- 用户提供的三段长文本在全拼和小鹤下的音节边界，以及每个汉字的单字兜底。

## 真实词库验证

```text
Loaded entries: 501935
hlheruhdlq       -> 黄河入海流（首选）
huangheruhailiu  -> 黄河入海流（首选）
lookup-word 黄河入海流 -> exact=yes
```

真实外部词库的 10,000 次核心查询基准：

```text
P50 22.1 us
P95 56.8 us
P99 106.3 us
```

这是核心词库查询耗时，不等同于 Windows 窗口绘制或首次宿主加载时间。

## 发布包

```text
artifacts/PiInput-v0.3.6-dev-windows-x64.zip
SHA-256: 8610f6e6b175fa555a4d0930e8ad41af1a4acc2ab2272a3b055e21a3fbaa8cfb
```

压缩包已检查，包含：

- 根目录 `PiInput-Install.exe`；
- 根目录 `PiInput-Test.exe`；
- `安装与使用指南.md`；
- `词库查询与分段取字.md`；
- `query-dictionary.cmd`；
- `bin/PiInputTSF.dll`、`bin/piinput_icon.ico` 和全部工具；
- `data/piinput-base.lex`、中文符号表及英文候选词库。

发布包内的 CLI 已使用随包词库重新验证小鹤、全拼及“黄河入海流”精确反查。

## 安装边界

本轮没有自动安装或切换系统输入法。用户将从发布 ZIP 完整解压后自行运行 `PiInput-Install.exe`，避免开发进程在未确认时影响当前输入环境。


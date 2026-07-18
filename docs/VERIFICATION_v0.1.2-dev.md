# LiteIME v0.1.2-dev 验证记录

## 1. 验证范围

- Linux Release 配置与完整编译；
- 核心单元测试；
- 两个真实 SCEL 文件回归测试；
- SCEL 信息读取与完整 TSV 转换；
- 标准拼音精确查询；
- Windows SDK `min` 宏冲突源代码修复检查；
- Visual Studio 2026 构建脚本静态检查。

## 2. Linux Release 构建

执行结果：

```text
liteime_core             built
liteime-cli              built
liteime-core-tests       built
liteime-scel-converter   built
```

## 3. 自动测试

```text
Test #1: liteime-core-tests       Passed
Test #2: liteime-scel-regression  Passed

100% tests passed, 0 tests failed out of 2
```

真实测试词库：

```text
电子词汇大全【官方推荐】     5,596 entries, 413 pinyin items
计算机词汇大全【官方推荐】  10,300 entries, 413 pinyin items
```

## 4. 工具链验证

完整转换：

```text
Converted 10300 entries to computer-full.tsv
```

查询：

```text
Loaded entries: 10300
1. 作用域    zuo'yong'yu    8848
```

Linux Release 二进制大小：

```text
liteime-scel-converter  75,920 bytes
liteime-cli             88,608 bytes
```

这些大小只代表当前 Linux 构建，不代表最终 Windows EXE 或输入法安装包大小。

## 5. 用户 Windows 实测输入

用户使用 Visual Studio 2026 Build Tools、CMake 和 Windows SDK 完成配置，证实：

- `liteime_core.lib` 可生成；
- `liteime-core-tests.exe` 可生成并通过；
- `liteime-cli.exe` 可生成；
- 原 v0.1.1-dev 的 `liteime-scel-converter` 在 `std::min` 处触发 C2589。

v0.1.2-dev 针对该日志修复。由于当前发布环境不是 Windows，本记录不冒充 Windows 复测已完成。

## 6. Windows 复测通过标准

重新执行：

```powershell
.\scripts\build_windows_vs2026.cmd
```

必须同时满足：

```text
配置成功
所有目标编译成功
测试成功
安装成功
dist/windows-x64/bin/liteime-cli.exe 存在
dist/windows-x64/bin/liteime-scel-converter.exe 存在
```

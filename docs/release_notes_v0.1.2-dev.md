# LiteIME v0.1.2-dev 发布说明

## 本版定位

这是 Windows Visual Studio 2026 首次真实编译反馈修复版。它修复了 SCEL 转换工具在 MSVC 下的编译错误，并强化 Windows 构建脚本的失败处理。

## 已修复

- 修复 `windows.h` 定义的 `min` 宏与 `std::min` 冲突导致的 MSVC `C2589`：
  - 新增 `include/liteime/windows_compat.h`；
  - 在包含 `windows.h` 前统一定义 `NOMINMAX`；
  - 同时定义 `WIN32_LEAN_AND_MEAN`，减少 Windows 头文件污染；
  - CMake 的 Windows 构建也统一定义 `NOMINMAX`、`WIN32_LEAN_AND_MEAN`、`UNICODE` 和 `_UNICODE`。
- Windows 构建脚本改名为：
  - `scripts/build_windows_vs2026.cmd`；
  - `scripts/build_windows_vs2026.ps1`。
- 构建脚本使用 `Visual Studio 18 2026` 生成器。
- 自动查找以下 Visual Studio 2026 CMake 路径，包括 Build Tools 位于 `Program Files (x86)` 的情况。
- 修复脚本在编译失败后仍继续执行测试和安装的问题：
  - 每个原生命令都显式检查 `$LASTEXITCODE`；
  - 失败立即退出并返回非零状态；
  - 安装前清理旧输出，避免误把旧 EXE 当成本次结果；
  - 安装后逐个验证预期 EXE 是否存在。
- CMake 工程版本从旧的 `0.1.0` 修正为 `0.1.2`。

## 用户反馈对应关系

用户 Windows 日志中：

```text
main.cpp(150,16): error C2589: “(”:“::”右边的非法标记
```

对应源码为：

```cpp
std::min(limit, dictionary.entries.size())
```

这是 Windows SDK 的 `min` 宏展开引起的，不是 SCEL 解析逻辑错误。

后续的：

```text
file INSTALL cannot find liteime-scel-converter.exe
```

只是前一个编译失败的连锁结果。测试通过只说明 `liteime_core` 单元测试通过，不代表整个解决方案已经构建成功。

# LiteIME v0.1.1-dev 验证记录

## 已完成验证

- Linux Release 构建通过；
- 现有 C++ 单元测试通过；
- SCEL 解析与查询回归测试通过；
- 发布包文件清单和 SHA-256 清单重新生成；
- Windows PowerShell 构建脚本完成静态审查：
  - 支持 PATH 中的 CMake；
  - 支持 Visual Studio 2022 自带 CMake；
  - 缺失依赖时提供明确错误信息；
  - 对 CMake、CTest、构建、测试、安装的退出码进行检查。

## 尚未完成验证

本发布环境没有 Windows、MSVC 和 Windows SDK，因此以下项目需要用户 Windows 电脑完成：

- PowerShell 脚本实际执行；
- Visual Studio 2022 生成器配置；
- Windows x64 Release 编译；
- `liteime-scel-converter.exe` 和 `liteime-cli.exe` 运行测试。

这部分不能在当前环境中假装已完成。

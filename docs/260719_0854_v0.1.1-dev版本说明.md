# PiInput v0.1.1-dev 发布说明

## 本版定位

这是针对 Windows 首次构建失败的开发环境修复版，不改变输入法核心功能。

## 修复

- 构建脚本不再直接假定 `cmake` 已加入系统 `PATH`；
- 优先使用 `PATH` 中的 CMake；
- 自动通过 `vswhere.exe` 查找 Visual Studio 2022 安装目录；
- 自动查找 Visual Studio 自带的 CMake；
- 自动查找与 CMake 配套的 `ctest.exe`；
- 缺少依赖时给出明确的中文安装指引；
- 每条外部构建命令均检查退出码；
- 构建完成后验证输出目录和 `.exe` 文件。

## 已知限制

当前发布环境不是 Windows，因此没有在本环境中生成或冒充 Windows `.exe`。Windows 构建仍需在用户安装了 Visual Studio 2022 C++ 工具链和 Windows SDK 的电脑上执行。

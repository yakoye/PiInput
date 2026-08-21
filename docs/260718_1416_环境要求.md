# Windows 构建前置条件

## Visual Studio 2026 Build Tools

安装以下组件：

- 使用 C++ 的桌面开发；
- C++ CMake tools for Windows；
- MSVC x64/x86 build tools；
- Windows 10 或 Windows 11 SDK。

项目脚本会自动寻找 Visual Studio 自带的 CMake，不要求其已加入 `PATH`。

## 构建

```powershell
.\scripts\build_windows_vs2026.cmd
```

清理重建：

```powershell
.\scripts\build_windows_vs2026.ps1 -Configuration Release -Clean
```

完整 SCEL 回归测试：

```powershell
.\scripts\build_windows_vs2026.ps1 `
  -Configuration Release `
  -TestDataDir "D:\Dicts"
```

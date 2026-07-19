# Windows 技术栈

## 主要语言

Windows 首版统一使用 C++20。

## 系统接入

- Text Services Framework（TSF）；
- COM；
- Win32；
- Windows 10/11 SDK；
- Visual Studio 2026 或 Visual Studio 2022 Build Tools / MSVC。

TSF 是正式输入法接入路径。不能把网页悬浮窗、全局键盘钩子或模拟按键作为正式输入法架构。

## v0.1.4-dev TSF 基线

当前 Windows 构建新增：

```text
PiInputTSF.dll
piinput-profile.exe
```

`PiInputTSF.dll` 当前实现：

- COM class factory；
- `DllRegisterServer` / `DllUnregisterServer`；
- 当前用户范围的 COM 注册；
- `ITfInputProcessorProfiles::Register`；
- 简体中文语言配置文件；
- 键盘文本服务分类；
- `ITfTextInputProcessor`；
- `ITfKeyEventSink`；
- `ITfCompositionSink`；
- 同步读写 edit session；
- Composition 创建、更新、提交、取消；
- GDI 最小候选窗口；
- 现有 C++ Engine、ImeSession、SymbolIndex 和 UserModel 接入。

`piinput-profile.exe` 当前负责：

- 激活和停用开发语言配置文件；
- 保存全拼或双拼方案；
- 查询当前方案。

这是最小 TSF 开发基线，不等同于正式可发布输入法。当前环境无法使用 Windows SDK/MSVC 编译，必须由用户 Windows 机器完成首次真实构建、注册和输入验证。

## UI

最终候选窗和符号面板计划：

- DirectWrite 绘制文字；
- Direct2D 绘制背景、选中态和边框；
- 原生窗口；
- 支持高 DPI、多显示器、深色模式和无障碍。

v0.1.4-dev 暂时使用 GDI 候选窗，只用于打通 TSF 输入链路。它不是最终 UI。

设置程序首版：

- C++20 + Win32 标准控件；
- 不为简单设置页引入 Electron、Qt 或大型 WebView 运行环境。

## 进程与 IPC

当前 v0.1.4-dev 为最小验证，TSF DLL 暂时直接链接输入核心。

正式架构：

```text
宿主应用进程
  └─ PiInputTSF.dll
          │ Named Pipe / ALPC 评估
          ▼
     PiInputEngine.exe
```

把大词库、用户数据库和复杂解码从宿主应用进程移到独立引擎，是后续稳定性任务。IPC 要求：

- 本地用户隔离；
- 消息有长度上限；
- 协议版本号；
- 超时；
- 崩溃重连；
- 候选 generation；
- 不传递裸指针；
- 不允许不受限反序列化。

## 构建

普通用户不需要手工指定生成器，直接运行：

```powershell
.\build.ps1 -Configuration Release -Clean
```

脚本依次探测 `Visual Studio 18 2026` 和 `Visual Studio 17 2022`，并自动使用 Visual Studio 自带的 CMake/CTest。

首版工具使用静态 MSVC 运行库，降低用户机器运行库依赖。TSF DLL 和最终安装器阶段需要继续评估运行库、签名和组件更新策略。

## 当前 Windows 产物

```text
piinput-scel-converter.exe
piinput-lexicon-compiler.exe
piinput-cli.exe
piinput-benchmark.exe
piinput-preview.exe
piinput-profile.exe
PiInputTSF.dll
```

后续规划：

```text
PiInputEngine.exe
PiInputConfig.exe
PiInput-Setup-vX.Y.Z-x64.exe 或 MSI
PiInputSync.exe（可选）
```

## 架构覆盖范围

v0.1.4-dev 只构建 x64 TSF DLL：

- x64 Windows 应用：目标范围；
- 32 位应用：尚未提供 x86 DLL；
- Windows ARM64：尚未提供 ARM64 构建；
- 正式发布前需要多架构安装和注册策略。

## Windows 头文件约束

包含 `windows.h` 前必须定义 `NOMINMAX` 和 `WIN32_LEAN_AND_MEAN`，避免 Windows SDK 的 `min`/`max` 宏破坏标准库调用。

所有 Windows 平台代码放在 `platform/windows`，不得把 TSF、COM、Win32 类型带入跨平台核心公共接口。

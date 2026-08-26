# PiInput v0.1.5-dev 版本说明

## 版本目标

根据用户在 Visual Studio 2026、Windows SDK 10.0.26100.0 环境中的第一轮 TSF 真实构建日志，修复阻断 `piinput-profile.exe` 和 `PiInputTSF.dll` 生成的 Windows SDK/链接问题。

## 用户真实构建结果

v0.1.4-dev 已经在用户 Windows 机器成功完成：

- CMake 配置；
- `piinput_core.lib`；
- 核心测试程序；
- SCEL 转换器；
- 词库编译器；
- CLI；
- benchmark；
- Win32 preview。

首次 TSF 构建暴露两个阻断问题：

```text
profile_tool.cpp: CLSID_TF_InputProcessorProfileMgr 未声明
PiInputTSF: LNK1181 无法打开 msctf.lib
```

同时发现两个非阻断警告：

```text
std::filesystem::u8path 在 C++20 中弃用
整数控件 ID 直接 reinterpret_cast 为 HMENU
```

## 根因与修复

### 1. Profile Manager COM 获取方式错误

`ITfInputProcessorProfileMgr` 是由 `CLSID_TF_InputProcessorProfiles` 对象暴露的接口，不存在可直接创建的 `CLSID_TF_InputProcessorProfileMgr`。

修改为：

```cpp
ITfInputProcessorProfiles* profiles = nullptr;
HRESULT result = CoCreateInstance(
    CLSID_TF_InputProcessorProfiles, nullptr,
    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles));

ITfInputProcessorProfileMgr* manager = nullptr;
result = profiles->QueryInterface(IID_PPV_ARGS(&manager));
```

激活和停用都使用同一获取方式。

### 2. 移除不存在的 msctf.lib 链接依赖

TSF 接口由 `msctf.h` 声明、运行时由 `Msctf.dll` 提供；本项目通过 COM 接口虚函数调用，不需要链接名为 `msctf.lib` 的导入库。

从以下目标移除 `msctf`：

```text
PiInputTSF
piinput-profile
```

保留：

```text
ole32
shell32
user32
gdi32
advapi32
uuid
```

### 3. 清理 C++20 文件系统警告

将弃用的：

```cpp
std::filesystem::u8path(value)
```

改为 Windows UTF-8 → UTF-16 转换后构造 `std::filesystem::path`。

### 4. 清理 64 位 HMENU 转换警告

控件 ID 先转换为 `INT_PTR`，再转换为 `HMENU`，避免 x64 下从 32 位整数直接转换为指针大小类型的警告。

### 5. 增加 Windows 源码回归测试

新增：

```text
tests/windows_source_regression.cmake
```

自动检查：

- 不得重新链接 `msctf`；
- 不得重新使用不存在的 `CLSID_TF_InputProcessorProfileMgr`；
- 必须通过 `CLSID_TF_InputProcessorProfiles` 获取 Profile Manager。

## 验证状态

已实际验证：

- Linux Release 构建；
- 核心测试；
- 两个真实 SCEL 回归；
- Windows TSF 源码回归检查；
- ASan/UBSan。

仍需用户 Windows 真机验证：

- `piinput-profile.exe` 是否编译成功；
- `PiInputTSF.dll` 是否链接成功；
- `regsvr32` 注册；
- Win+Space 是否出现 PiInput；
- 记事本中的 Composition、候选窗口和上屏。

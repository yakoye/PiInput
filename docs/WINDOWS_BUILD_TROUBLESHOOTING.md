# Windows 构建故障记录

## 1. PowerShell 找不到 cmake

现象：

```text
cmake : The term 'cmake' is not recognized
```

原因：Visual Studio 自带 CMake 已安装，但目录没有加入用户 `PATH`。

项目脚本不再要求手动修改 `PATH`，会自动通过 `vswhere.exe` 和常见安装目录查找，例如：

```text
C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
```

## 2. C2589 出现在 std::min

现象：

```text
main.cpp(150,16): error C2589: “(”:“::”右边的非法标记
```

根因：Windows SDK 的 `windows.h` 默认定义 `min` 和 `max` 宏，导致：

```cpp
std::min(a, b)
```

在预处理阶段被错误展开。

项目采用两层修复：

```cpp
#define NOMINMAX
#include <windows.h>
```

并在敏感位置写成：

```cpp
(std::min)(a, b)
```

以后新增 Windows 源文件时，不得在未定义 `NOMINMAX` 的情况下直接包含 `windows.h`。

## 3. 安装阶段找不到 EXE

现象：

```text
file INSTALL cannot find ... liteime-scel-converter.exe
```

这通常不是安装规则本身的问题，而是前面的编译已经失败，目标 EXE 根本没有生成。

正确脚本必须在 `cmake --build` 返回非零后立即终止。v0.1.2-dev 已修复。

## 4. 测试通过但整体构建仍失败

`liteime-core-tests` 只链接核心库。某个独立工具目标编译失败时，核心测试仍可能通过。因此必须同时满足：

1. 配置成功；
2. 所有目标编译成功；
3. 测试成功；
4. 安装成功；
5. 两个预期 EXE 均存在。

不能只凭 `100% tests passed` 判断完整构建成功。

## 5. CLSID_TF_InputProcessorProfileMgr 未声明

现象：

```text
error C2065: CLSID_TF_InputProcessorProfileMgr 未声明
```

根因：`ITfInputProcessorProfileMgr` 是接口，不存在同名可直接创建的 COM class。应创建 `CLSID_TF_InputProcessorProfiles`，并查询 `ITfInputProcessorProfileMgr`。

正确写法：

```cpp
ITfInputProcessorProfiles* profiles = nullptr;
HRESULT result = CoCreateInstance(
    CLSID_TF_InputProcessorProfiles, nullptr,
    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles));

ITfInputProcessorProfileMgr* manager = nullptr;
result = profiles->QueryInterface(IID_PPV_ARGS(&manager));
```

## 6. LNK1181 找不到 msctf.lib

现象：

```text
LINK : fatal error LNK1181: 无法打开输入文件 msctf.lib
```

根因：项目把 TSF 运行时 DLL 名称误当成链接库名。当前实现通过 COM 接口调用，不需要 `msctf.lib`。

处理：

- 从 CMake 的 `target_link_libraries` 移除 `msctf`；
- 保留实际需要的 `ole32`、`uuid`、`user32` 等 Windows SDK 库；
- 不要通过安装额外 SDK 或复制未知 `.lib` 掩盖错误链接配置。


## `DeactivateProfile failed: 0x80004005`

含义：旧 LiteIME 配置文件不存在、未启用或当前没有处于激活状态。对“修复注册”和“升级安装”来说，这是可接受的清理状态，不代表新版注册失败。

从 `v0.1.6-dev` 开始，脚本不会在这里停止，而会继续：

```text
注销旧 DLL
→ 注册新版 DLL
→ RegisterProfile
→ ActivateProfile
→ --status 验证
```

不要只看清理阶段信息，最终以以下输出为准：

```text
registered=yes
enabled=yes
```

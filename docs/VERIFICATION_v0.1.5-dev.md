# PiInput v0.1.5-dev 验证记录

验证日期：2026-07-18

## 1. Windows 真机输入日志基线

用户环境：

```text
Visual Studio 18 2026
MSVC 19.51.36248.0
Windows SDK 10.0.26100.0
目标系统 Windows 10.0.26200
```

v0.1.4-dev 实际成功生成：

```text
piinput_core.lib
piinput-core-tests.exe
piinput-scel-converter.exe
piinput-lexicon-compiler.exe
piinput-cli.exe
piinput-benchmark.exe
piinput-preview.exe
```

实际失败：

```text
profile_tool.cpp(67,39): CLSID_TF_InputProcessorProfileMgr 未声明
profile_tool.cpp(85,39): CLSID_TF_InputProcessorProfileMgr 未声明
LNK1181: 无法打开输入文件 msctf.lib
```

因此 v0.1.4 的 TSF DLL 没有生成，安装和注册流程未执行。

## 2. v0.1.5 修复检查

源码已确认：

```text
profile_tool.cpp 使用 CLSID_TF_InputProcessorProfiles
profile_tool.cpp 使用 IID_PPV_ARGS(&manager)
CMake 不再为 PiInputTSF 链接 msctf
CMake 不再为 piinput-profile 链接 msctf
```

新增自动测试：

```text
piinput-windows-source-regression
```

用于阻止以上错误再次进入发布包。

## 3. Linux Release 构建

执行：

```bash
cmake -S . -B build/linux \
  -DCMAKE_BUILD_TYPE=Release \
  -DPIINPUT_BUILD_TESTS=ON \
  -DPIINPUT_TESTDATA_DIR=/mnt/data
cmake --build build/linux --parallel 2
ctest --test-dir build/linux --output-on-failure
```

结果：

```text
3/3 tests passed
```

测试项：

```text
piinput-core-tests
piinput-windows-source-regression
piinput-scel-regression
```

## 4. Sanitizer

执行 ASan/UBSan Debug 构建和测试。

结果：

```text
3/3 tests passed
未报告 AddressSanitizer 错误
未报告 UndefinedBehaviorSanitizer 错误
未报告泄漏错误
```

## 5. Windows 警告清理

根据用户日志修复：

- C++20 `std::filesystem::u8path` 弃用警告；
- Win32 控件 ID 到 `HMENU` 的 x64 转换警告。

当前环境没有 Windows SDK/MSVC，无法在本地证明这些改动已经通过用户的 VS 2026 编译。必须由用户重新运行：

```powershell
.\setup-dev.cmd
```

## 6. 本版 Windows 验收条件

必须同时满足：

1. `piinput-profile.exe` 编译成功；
2. `PiInputTSF.dll` 链接成功；
3. CTest 全部通过；
4. 安装阶段找到所有 EXE/DLL；
5. `regsvr32` 成功；
6. profile 激活成功；
7. Win+Space 出现 PiInput；
8. 记事本小鹤输入 `jisrji` 后出现“计算机”；
9. Space 上屏成功。

在收到这轮真实结果前，只能声明：

> 已针对用户日志修复 Windows 首轮编译阻断，等待 Windows 重编译和 TSF 运行验证。

# PiInput v0.1.4-dev 验证记录

验证日期：2026-07-18

## 1. 环境边界

当前自动验证环境是 Linux，具备 GCC/CMake，但没有 Windows SDK、MSVC 和真实 TSF 宿主环境。

因此本记录分为：

- 已实际执行并通过的跨平台核心测试；
- 对 Windows TSF 源码进行的受限语法审查；
- 必须由用户 Windows 机器继续完成的验证。

不得把第二项写成 Windows TSF 已真实编译或已在应用中正常工作。

## 2. Release 构建

执行：

```bash
cmake -S . -B build/linux \
  -DCMAKE_BUILD_TYPE=Release \
  -DPIINPUT_BUILD_TESTS=ON \
  -DPIINPUT_TESTDATA_DIR=/mnt/data
cmake --build build/linux --parallel 2
ctest --test-dir build/linux --output-on-failure
cmake --install build/linux --prefix dist/linux-x64
```

结果：

```text
Release 配置成功
全部跨平台目标编译成功
2/2 CTest 通过
安装步骤成功
```

测试：

```text
piinput-core-tests        Passed
piinput-scel-regression   Passed
```

## 3. Sanitizer

执行：

```bash
cmake -S . -B build/asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPIINPUT_BUILD_TESTS=ON \
  -DPIINPUT_TESTDATA_DIR=/mnt/data \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build/asan --parallel 2
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build/asan --output-on-failure
```

结果：

```text
2/2 tests passed
未报告 AddressSanitizer 错误
未报告 UndefinedBehaviorSanitizer 错误
未报告泄漏错误
```

## 4. 用户真实 SCEL 回归

验证文件：

```text
电子词汇大全【官方推荐】.scel
计算机词汇大全【官方推荐】.scel
```

结果：

```text
电子词库：5,596 条，413 个拼音项
计算机词库：10,300 条，413 个拼音项
回归测试通过
```

## 5. v0.1.3 无候选问题回归

内置词库条目数：

```text
311
```

全拼：

```text
input: jisuanji
pinyin: ji'suan'ji
candidate: 计算机
```

小鹤双拼：

```text
input: jisrji
pinyin: ji'suan'ji
candidate: 计算机
```

句子：

```text
input: woxiangxuexixieyi
candidate: 我想学习协议
```

符号：

```text
query: sheshidu
candidate: ℃
```

以上均通过实际 CLI 执行验证。

## 6. 小词库查询基准

命令：

```bash
./build/linux/piinput-benchmark \
  --lexicon data/base_lexicon.tsv \
  --schema flypy \
  --query jisrji \
  --iterations 10000
```

样本结果：

```text
lexicon_entries=311
load_ms=0.862
average_us=0.914
p50_us=0.881
p95_us=0.912
p99_us=1.813
max_us=17.065
```

这是 Linux 环境中起步小词库的核心查询数据，不等同于 Windows TSF 完整按键延迟，也不能用于推断大规模正式词库性能。

## 7. Windows TSF 源码审查

新增源码：

```text
platform/windows/tsf/PiInputTSF.def
platform/windows/tsf/piinput_tsf_guids.h
platform/windows/tsf/candidate_window.h
platform/windows/tsf/candidate_window.cpp
platform/windows/tsf/text_service.h
platform/windows/tsf/text_service.cpp
platform/windows/tsf/dllmain.cpp
platform/windows/tsf/profile_tool.cpp
```

已完成：

- CMake 目标和链接库检查；
- COM class factory 生命周期检查；
- 注册、注销和失败回滚检查；
- TSF profile 参数与官方接口签名交叉检查；
- edit session 和 composition 调用接口交叉检查；
- 使用最小 Windows 头文件替身进行受限语法检查；
- `candidate_window.cpp` 语法检查通过；
- `dllmain.cpp` 语法检查通过；
- 在排除 Linux libstdc++ 对 `_WIN32` 路径模拟差异后，`text_service.cpp` 和 `profile_tool.cpp` 语法检查通过。

该检查不能发现：

- MSVC 专有编译差异；
- Windows SDK 类型、GUID 或链接错误；
- regsvr32 注册错误；
- TSF 生命周期和宿主兼容问题；
- 候选窗口真实位置问题。

## 8. Windows 待验证清单

用户解压本版后执行：

```powershell
.\setup-dev.cmd
```

必须继续确认：

1. `PiInputTSF.dll` 编译和链接成功；
2. `piinput-profile.exe` 编译成功；
3. 完整测试通过；
4. regsvr32 成功；
5. Win+Space 出现 PiInput；
6. 记事本输入 `jisrji` 有 Composition；
7. 候选窗显示“计算机”；
8. Space 上屏“计算机”；
9. Esc、Backspace、数字键和翻页工作；
10. 卸载后 profile 消失，用户数据默认保留。

在收到这些真实输出前，v0.1.4 的 TSF 状态为：

> 源码开发完成，等待 Windows 首次编译和真机输入验证。

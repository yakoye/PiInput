# PiInput v0.1.3-dev 验证记录

验证日期：2026-07-18

## 1. 验证范围

本记录验证：

- C++20 核心能否完整配置、编译和链接；
- 自动测试；
- 用户提供的两个真实 SCEL；
- SCEL → TSV → `.lex`；
- 全拼和小鹤双拼；
- 句子组合；
- 符号检索；
- 性能基准工具；
- AddressSanitizer 和 UndefinedBehaviorSanitizer。

Windows 专用的 `piinput-preview.exe` 只能在 Windows/MSVC 环境构建。本发布环境是 Linux，因此本记录不冒充 Windows 编译已经完成。Windows 构建需要用户运行根目录 `setup-dev.cmd` 后反馈日志。

## 2. Release 构建和测试

执行：

```bash
rm -rf build/linux dist/linux-x64
./scripts/build_linux.sh /mnt/data
```

结果：

```text
所有核心和工具目标编译、链接成功
piinput-core-tests: passed
piinput-scel-regression: passed
2/2 tests passed
```

生成：

```text
piinput-scel-converter
piinput-lexicon-compiler
piinput-cli
piinput-benchmark
```

## 3. 真实 SCEL 回归

验证文件：

```text
电子词汇大全【官方推荐】.scel
计算机词汇大全【官方推荐】.scel
```

自动断言：

```text
电子词库：413 个拼音项，5,596 条词条
计算机词库：413 个拼音项，10,300 条词条
```

结果：通过。

## 4. 词库转换和二进制编译

执行：

```bash
piinput-scel-converter \
  --input 计算机词汇大全【官方推荐】.scel \
  --output computer.tsv \
  --format tsv

piinput-lexicon-compiler \
  --input computer.tsv \
  --output computer.lex
```

结果：

```text
Converted 10300 entries
Compiled 10300 entries
computer.lex: 约 483 KiB
```

自动测试还验证：相同 `word+pinyin` 的重复词条会合并，并保留最高权重。

## 5. 全拼查询

执行：

```bash
piinput-cli \
  --lexicon computer.lex \
  --query zuoyongyu \
  --schema full \
  --top 5
```

结果：

```text
1. 作用域    zuo'yong'yu    8848
```

## 6. 小鹤双拼

执行：

```bash
piinput-cli \
  --lexicon examples/sample_lexicon.tsv \
  --query jisrji \
  --schema flypy \
  --show-decode
```

结果：

```text
Decoded pinyin: ji'suan'ji
1. 计算机
2. 计蒜机
```

自动测试同时覆盖：小鹤、自然码、微软、智能 ABC。

## 7. 句子组合

执行：

```bash
piinput-cli \
  --lexicon examples/sample_lexicon.tsv \
  --query woxiangxuexixieyi \
  --schema full
```

结果：

```text
1. 我想学习协议
```

## 8. 符号搜索

执行：

```bash
piinput-cli \
  --symbols data/symbols.tsv \
  --symbol-query sheshidu
```

结果：

```text
Loaded symbols: 140
1. ℃    摄氏度    单位符号
```

## 9. Linux 容器性能样本

执行：

```bash
piinput-benchmark \
  --lexicon computer.lex \
  --schema full \
  --query zuoyongyu \
  --warmup 100 \
  --iterations 3000
```

本次样本：

```text
lexicon_entries=10300
load_ms=9.182
average_us=1.959
p50_us=1.613
p95_us=3.135
p99_us=3.455
max_us=21.492
```

这些数字只代表当前 Linux 容器中的精确查询样本，不能直接当作 Windows TSF 按键到候选显示延迟。Windows 真机仍需单独测量 IPC、TSF、UI 和系统调度开销。

## 10. ASan/UBSan

执行 Debug + AddressSanitizer + UndefinedBehaviorSanitizer：

```text
piinput-core-tests: passed
piinput-scel-regression: passed
2/2 tests passed
未报告内存越界、Use-After-Free 或未定义行为
```

## 11. Windows 待用户验证

在 Windows 的 `piinput-dev` 根目录运行：

```powershell
.\setup-dev.cmd
```

预期生成：

```text
dist\windows-x64\bin\piinput-scel-converter.exe
dist\windows-x64\bin\piinput-lexicon-compiler.exe
dist\windows-x64\bin\piinput-cli.exe
dist\windows-x64\bin\piinput-benchmark.exe
dist\windows-x64\bin\piinput-preview.exe
```

随后运行：

```powershell
.\start-preview.cmd
```

需要重点确认：

- MSVC `/W4` 编译结果；
- Win32 预览启动；
- 中文路径词库加载；
- 四种双拼候选；
- `;sheshidu` 符号搜索；
- 双击复制和 `%LOCALAPPDATA%\PiInput\UserData\user_model.tsv` 写入；
- 开始菜单快捷方式；
- 卸载后用户数据是否保留。

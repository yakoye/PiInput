# v0.1.0-dev 验证记录

验证日期：2026-07-17

## 环境

```text
Linux x86-64
CMake 3.31.6
G++ 14.2.0
Ninja 1.12.1
C++20 Release
```

## 构建命令

```bash
cmake -S . -B build/linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLITEIME_TESTDATA_DIR=/mnt/data
cmake --build build/linux --parallel
```

结果：构建退出码 0，生成：

```text
liteime-scel-converter
liteime-cli
liteime-core-tests
```

## 测试命令

```bash
ctest --test-dir build/linux --output-on-failure
```

实际结果：

```text
2/2 tests passed
0 tests failed
Total Test time: 0.03 sec
```

测试包括：

- 开发 TSV 词库加载和权重排序；
- 非法 SCEL 拒绝；
- 电子词库真实文件回归；
- 计算机词库真实文件回归。

## SCEL 信息验证

电子词库：

```json
{
  "title": "电子词汇大全【官方推荐】",
  "category": "电子工程",
  "description": "官方推荐，词库来源于网友上传！",
  "format_mask": 68,
  "pinyin_count": 413,
  "entry_count": 5596
}
```

计算机词库：

```json
{
  "title": "计算机词汇大全【官方推荐】",
  "category": "计算机科技",
  "description": "官方推荐，词库来源于网友上传！",
  "format_mask": 68,
  "pinyin_count": 413,
  "entry_count": 10300
}
```

## 转换验证

```text
计算机词库完整 TSV：10,300 条
文件总行数：10,305（包含注释和表头）
大小：约 352 KiB
```

## 查询验证

命令：

```bash
liteime-cli \
  --lexicon computer_full.tsv \
  --query "zuo'yong'yu" \
  --top 10
```

结果：

```text
Loaded entries: 10300
1. 作用域    zuo'yong'yu    8848
```

## Windows 状态

本次执行环境没有 MSVC 和 Windows SDK，因此没有声称 Windows `.exe` 已构建或运行。项目包含 Visual Studio 2022 构建脚本；Windows 输出必须在 Windows 环境重新构建、运行测试并记录结果后才可标记为已验证。

## Sanitizer 验证

使用 AddressSanitizer 和 UndefinedBehaviorSanitizer 重新构建并执行全部测试：

```bash
cmake -S . -B build/sanitize -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DLITEIME_TESTDATA_DIR=/mnt/data
cmake --build build/sanitize --parallel
ASAN_OPTIONS=detect_leaks=1 \
  ctest --test-dir build/sanitize --output-on-failure
```

实际结果：

```text
2/2 tests passed
0 tests failed
Total Test time: 0.26 sec
```

本次运行未报告 AddressSanitizer 或 UndefinedBehaviorSanitizer 错误。

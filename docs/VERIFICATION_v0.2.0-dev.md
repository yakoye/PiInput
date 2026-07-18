# LiteIME v0.2.0-dev 验证记录

## 环境

- Windows 11 SDK 10.0.26100.0
- Visual Studio 18 2026 / MSVC 19.51
- Release x64

## 构建与自动测试

Windows Release 构建覆盖核心库、TSF DLL、预览程序、CLI、SCEL 转换器、二进制词库编译器、词库构建器和 benchmark。

CTest 项目：

```text
liteime-core-tests
liteime-windows-source-regression
liteime-dictionary-script-regression
liteime-performance-smoke
liteime-scel-regression
```

## 真实本地词库

数据目录：`C:\Users\color\Downloads\lite-ime\dicts`

来源：pinyin-data、phrase-pinyin-data、rime-pinyin-simp、THUOCL（仅缓存）、电子 SCEL、计算机 SCEL。生成二进制词条数：459,505。

关键结果：

```text
gjjt  → gan'jue   → 1. 感觉
xmzd  → xian'zai  → 1. 现在
qq    → qiu       → 1. 秋
wo    → wo        → 1. 我
```

10,000 次 `wo` Release 核心查询的实测样本：

```text
load_ms=246.442
average_us=10.220
p50_us=10.000
p95_us=10.600
p99_us=12.600
max_us=174.300
```

该数据只代表核心查询，不等同于 TSF 窗口绘制端到端延迟。

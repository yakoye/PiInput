# PiInput v0.3.7-dev 验证记录

验证日期：2026-08-11  
验证平台：Windows x64、Visual Studio 18 2026、Windows SDK 10.0.26100.0

## 本版目标

- 英文模式从第一个字母开始提供离线候选。
- 原始输入始终保留为第一候选。
- 扩大内置英文词库，覆盖日常、财经、科技和用户给出的英文测试段落。
- 精确前缀候选优先；仅在候选不足时追加有界模糊补全。
- 保持中文全拼、小鹤双拼、分段取字和现有安装流程不回归。

## 构建与自动测试

- Release 全目标构建：通过。
- CTest：31/31 通过，0 失败，总耗时 76.91 秒。
- `piinput-english-lexicon`：通过。
- `piinput-windows-source-regression`：通过。
- `piinput-external-dictionary-regression`：通过。
- `piinput-paragraph-input-regression`：通过。
- `piinput-character-coverage`：通过。

## 英文候选验证

- 内置英文高频词库：24,323 条 ASCII 词条。
- 产品补充词表：12 条。
- 英文测试段落被解析为不少于 80 个唯一单词，测试要求每个单词均可通过候选到达。
- 长度超过 3 的测试单词，必须在完整输入前产生补全候选。
- 已覆盖 `r`、`re`、`rev`、`reve`、`b`、`bo`、`boo`、`book` 的逐键候选。
- 原始输入为第 1 候选；词典精确前缀排在模糊候选之前。
- `reve` 可补全到 `revile`，同时保留 `reverse` 等精确前缀结果。
- `book` 可获得 `books`、`booked` 等补全。

## 中文回归验证

使用打包词库 `data/piinput-base.lex`，共加载 501,935 条：

```text
小鹤：hlheruhdlq       -> 黄河入海流（第 1 候选）
全拼：huangheruhailiu  -> 黄河入海流（第 1 候选）
```

## 词库来源与许可

英文高频词来自固定版本的 `wordfreq-en-25000` 数据，源数据 SHA-256：

```text
51cc5521d1ff8cf4353f72199fd4d3ce3cbff9ca9e61858414b94314c9df35a6
```

转换结果为 24,323 条大小写精确去重的 ASCII 词条。词库数据遵循 CC BY-SA 4.0，来源与许可已写入 `LICENSE_NOTICE.md`。

## 发布包

```text
artifacts/PiInput-v0.3.7-dev-windows-x64.zip
SHA-256: 121eeb8824162125b80a7d560b1ffe50442a4f74ea352c6868e4f1a98e8d3e18
```

包内已核对：

- `PiInput-Install.exe`
- `PiInput-Test.exe`
- `bin/PiInputTSF.dll`
- `bin/piinput_icon.ico`
- `data/piinput-base.lex`
- `data/english_lexicon.tsv`
- `data/english_supplement.tsv`
- `安装与使用指南.md`
- `LICENSE_NOTICE.md`

本轮没有自动安装、没有提交、没有推送仓库。安装与实际输入体验由用户在解压后自行验证。

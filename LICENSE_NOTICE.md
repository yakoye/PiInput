# License Notice

PiInput 尚未确定公开发布许可证。除非后续版本明确附带许可证，否则源代码和文档仅用于当前项目开发、测试和评审。

用户提供的第三方 SCEL 文件及其转换结果不因进入测试流程而改变原有权利状态。公开发布时不得默认打包没有明确再分发授权的第三方完整词库。

发布用中文候选由外部本地 `dicts/rime-ice-full` 中的 `iDvel/rime-ice` 构建，
`data/piinput-base.lex` 是其转换结果。Rime Ice 仓库标注为 GPL-3.0；发布该转换结果时应同时
保留来源与相应许可证信息。其他旧中文源不再合并进发布候选。

以下可选开发数据保存在外部本地 `dicts/sources`，不随本仓库重新分发：

- `mozillazg/pinyin-data`：MIT；
- `mozillazg/phrase-pinyin-data`：MIT；
- `rime/rime-pinyin-simp`：Apache-2.0；
- `thunlp/THUOCL`：MIT，仅作为可选领域来源下载，默认不合并。
- `aparrish/wordfreq-en-25000`：数据采用 CC BY-SA 4.0，来源仓库
  `https://github.com/aparrish/wordfreq-en-25000`。可选英文更新固定到 commit
  `9650fa612e121beb6126b9b4d7344da287013c6e`，原始文件
  `wordfreq-en-25000-log.json` 的 SHA-256 为
  `51cc5521d1ff8cf4353f72199fd4d3ce3cbff9ca9e61858414b94314c9df35a6`。
  转换后的 `data/english_lexicon.tsv` 作为独立数据集按 CC BY-SA 4.0 随 PiInput
  Windows 测试包分发；源码与其他数据不因此自动改用相同许可证。完整来源文件仍只
  下载到外部 `dicts/sources`。许可证全文见
  `https://creativecommons.org/licenses/by-sa/4.0/`。

PiInput 不提取或重新分发已安装商业输入法的内部基础词库。

随包 `bin/yesymbol.exe` 固定来自 YeTools `yesymbol-dev` 的 `v1.1.1`，YeSymbol
本体采用 MIT License。其嵌入的 Twemoji 图形采用 CC BY 4.0，Unicode/CLDR
数据适用 Unicode License，完整许可证、第三方声明和来源哈希位于
`bin/licenses/YeSymbol/`。

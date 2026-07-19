# PiInput Test Corpus v0.2.0

面向 PiInput 中文输入法项目的第二版测试语料与评估工具包。

## 项目定位

本包不是单纯的一段中文测试文字，而是一套可扩展的输入法验证基础设施：

- 人工可复制的 Markdown/TXT 测试语料；
- 全拼与小鹤双拼方案配置；
- 长句、上下文消歧、专业词库和切分歧义语料；
- 可追溯的容错纠错与模糊音用例；
- 用户词频学习、删除、重置和隐私测试；
- Top-1、Top-5、目标排名、整句匹配、字符错误率和延迟评估脚本；
- 后续更多双拼、高级功能和平台兼容性的扩展接口。

## v0.2.0 新增范围

- 20 条长句测试，覆盖日常、输入法、PCIe、固件、驱动和操作系统；
- 24 组、48 条上下文消歧对照句；
- 16 条拼音切分歧义用例；
- 59 条专业词汇用例；
- 160 条自动生成的全拼/小鹤纠错用例；
- 20 条模糊音开启/关闭用例；
- 12 条用户学习和隐私测试；
- 候选结果模板、样例结果和自动评估报告。

## 目录

```text
piinput-test-corpus-v0.2.0/
├─ README.md
├─ docs/                 项目说明、测试计划、指标、路线图和续接说明
├─ schemes/              全拼与双拼方案配置
├─ corpus/               长句、消歧、专业词、纠错种子和模糊音语料
├─ keyboard/             可打印按键、通用动作键和邻接关系
├─ tests/                结构化测试用例
├─ reports/              引擎结果模板与演示数据
├─ tools/                生成、评估、校验和发布脚本
└─ generated/            人工测试语料与样例评估报告
```

## 快速使用

```bash
python tools/build_release.py
```

或者逐项执行：

```bash
python tools/generate_double_pinyin_cases.py
python tools/generate_language_model_cases.py
python tools/generate_typo_cases.py
python tools/generate_fuzzy_cases.py
python tools/generate_human_corpus.py
python tools/evaluate_candidate_results.py reports/sample_engine_results.json
python tools/validate_test_corpus.py
```

人工测试直接打开：

- `generated/Chinese_IME_Test_Corpus_v0.2.0.md`
- `generated/Chinese_IME_Test_Corpus_v0.2.0.txt`
- `keyboard/manual_key_checklist.md`

## 设计约束

1. 双拼编码必须由方案配置自动生成，不手工维护长句编码。
2. 候选质量使用指标记录，不假设引擎必须采用 HMM、Viterbi、N-gram 或神经模型。
3. 纠错关闭时验证严格和稳定行为；纠错开启时记录目标可达性与排名，不强迫所有错码首选命中。
4. 模糊音必须成对测试开启与关闭状态。
5. 密码框和隐私模式不得污染用户词频。
6. Windows/macOS、ANSI/ISO/JIS 和外接键盘的完整兼容性固定安排在 v0.5.0。
7. 所有文本和 JSON 使用 UTF-8。

## 当前限制

- 尚未连接 PiInput 的真实候选接口；`reports/sample_engine_results.json` 仅用于演示报告格式。
- 推荐排名阈值是测试基线，不代表所有产品必须采用同一候选策略。
- v0.5.0 前不对具体平台扫描码和物理键盘映射作完整断言。

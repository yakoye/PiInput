# 输入法三组文本逐字准确对照测试 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 使用搜狗与 PiInput 实机完成三组文本的逐字准确输入，并生成可复核的候选路径、按键成本和差异报告。

**Architecture:** 使用独立的候选探查窗口确定自然词组的候选路径，再在独立正式窗口执行经过验证的路径。正式正文只追加，不在已确认边界之前执行删除或撤销。

**Tech Stack:** Windows TSF、Computer Use、Notepad++、Notepad4、PowerShell 只读校验、UTF-8/Unicode 文本比较。

## Global Constraints

- 不修改 PiInput 生产代码、词库和用户学习数据。
- 搜狗和 PiInput 使用相同文本、相同小鹤双拼编码和相同自然分词规则。
- 最终文本必须逐字、逐数字、逐标点一致。
- 已确认正文永不删除；Escape 和回退只作用于当前组合区。

---

### Task 1: 建立不可回退的测试边界

**Files:**
- Create: `artifacts/sogou-piinput-corpus-comparison/exact-test-state.json`
- Create: `artifacts/sogou-piinput-corpus-comparison/exact-input-actions.csv`

**Interfaces:**
- Consumes: `tests/data/real_world_text_corpus.txt`
- Produces: 每个片段的目标文本、输入码、确认边界和候选路径。

- [ ] 从三组语料生成保留数字、英文、标点和换行的测试片段。
- [ ] 为搜狗和 PiInput 各选择一个探查窗口和一个正式窗口。
- [ ] 清空四个窗口并记录初始长度为零。
- [ ] 验证取消探查组合不会改变正式窗口。
- [ ] 验证每个确认键之前都存在一份最新候选截图或候选文本记录。

### Task 2: 执行搜狗逐字准确输入

**Files:**
- Create: `artifacts/sogou-piinput-corpus-comparison/sogou-exact-final.txt`
- Modify: `artifacts/sogou-piinput-corpus-comparison/exact-input-actions.csv`

**Interfaces:**
- Consumes: Task 1 的测试片段。
- Produces: 搜狗完整正文及候选路径统计。

- [ ] 先尝试剩余内容的最长自然短句；首选错误时只拆出前面的高把握词，再重新匹配剩余长段。
- [ ] 在探查窗口逐行查找正确候选，找不到才缩短片段。
- [ ] 对每条路径记录空格确认、数字选词以及“已选前缀继续处理剩余拼音”的操作。
- [ ] 每输入一个字母后观察候选变化，目标候选未出现时禁止提交。
- [ ] 在正式窗口执行确认路径，片段完成后更新确认边界。
- [ ] 每组文本结束后复制并与目标文本精确比较。

### Task 3: 执行 PiInput 逐字准确输入

**Files:**
- Create: `artifacts/sogou-piinput-corpus-comparison/piinput-exact-final.txt`
- Modify: `artifacts/sogou-piinput-corpus-comparison/exact-input-actions.csv`

**Interfaces:**
- Consumes: Task 1 使用的完全相同测试片段。
- Produces: PiInput 完整正文及候选路径统计。

- [ ] 切换并确认 PiInput 已激活。
- [ ] 按与搜狗相同的动态分段规则输入，不使用预先固定的逐词机械切分。
- [ ] 在探查窗口逐行查找候选，记录 `=`、方向键和数字选词路径。
- [ ] 每输入一个字母后观察候选变化，并在确认键之前核对目标候选文字。
- [ ] 在正式窗口执行确认路径，禁止越过确认边界回退。
- [ ] 每组文本结束后复制并与目标文本精确比较。

### Task 4: 汇总最终比较报告

**Files:**
- Create: `artifacts/sogou-piinput-corpus-comparison/exact-comparison-metrics.json`
- Create: `artifacts/sogou-piinput-corpus-comparison/EXACT_COMPARISON.md`

**Interfaces:**
- Consumes: Task 2 和 Task 3 的最终文本及动作表。
- Produces: 两款输入法的准确完成成本和候选策略结论。

- [ ] 断言两份最终文本均与目标文本完全一致。
- [ ] 统计字母、空格、数字选词、翻行、缩短重输、取消和耗时。
- [ ] 列出搜狗更省操作、PiInput 更省操作以及双方相同的代表片段。
- [ ] 明确区分首选命中率与修正后完整输入成本。

### Task 5: 重复输入学习效果

**Files:**
- Modify: `artifacts/sogou-piinput-corpus-comparison/exact-input-actions.csv`
- Create: `artifacts/sogou-piinput-corpus-comparison/learning-pass-metrics.json`

**Interfaces:**
- Consumes: 两款输入法第 1 遍完整准确输入路径。
- Produces: 第 1、2、3 遍候选排名和操作成本变化。

- [ ] 使用完全相同的自然分段方法，分别完成原三组文本第 2 遍输入。
- [ ] 完成原三组文本第 3 遍输入。
- [ ] 对比每遍的首选变化、数字选词、翻行、重输、总按键和耗时。
- [ ] 将当前用户状态下的第一遍与全新安装冷启动明确区分。

### Task 6: 未见文本泛化测试

**Files:**
- Consume: `tests/data/unseen_generalization_corpus_2026-08-15.txt`
- Create: `artifacts/sogou-piinput-corpus-comparison/unseen-sogou-final.txt`
- Create: `artifacts/sogou-piinput-corpus-comparison/unseen-piinput-final.txt`
- Create: `artifacts/sogou-piinput-corpus-comparison/unseen-comparison-metrics.json`

**Interfaces:**
- Consumes: 三组此前未输入、未调优的文本。
- Produces: 自然中文、文言文和中英文混排的首次输入泛化结果。

- [ ] 在原三组文本三遍完成后，首次向搜狗输入三组未见文本。
- [ ] 首次向 PiInput 输入完全相同的未见文本。
- [ ] 校验两份最终输出逐字、逐数字、逐标点一致。
- [ ] 分类别比较候选命中、修正成本和中英文状态切换成本。

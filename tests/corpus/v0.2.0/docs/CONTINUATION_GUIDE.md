# 新会话续接说明

## 当前版本

`piinput-test-corpus-v0.2.0`

## 当前状态

- 已完成全拼、小鹤双拼和基础字符测试结构；
- 已完成长句、上下文消歧、切分歧义和专业词库；
- 已生成全拼/小鹤纠错用例和模糊音成对测试；
- 已定义用户学习、隐私和损坏恢复测试；
- 已实现候选质量评估和一键发布脚本；
- 平台兼容性固定为 v0.5.0；
- 下一版本为 v0.3.0“更多双拼方案”。

## 下一次继续时优先读取

1. `README.md`
2. `docs/ROADMAP.md`
3. `docs/NEXT_DEVELOP_PLAN_v0.3.0.md`
4. `docs/METRICS_AND_REPORT_FORMAT.md`
5. `schemes/xiaohe.json`
6. `tools/generate_language_model_cases.py`
7. `tools/generate_typo_cases.py`

## 不得丢失的约束

- 双拼编码必须自动生成；
- 测试包必须带完整 Markdown 文档；
- 测试 ID 不得无故复用或改变含义；
- 平台兼容性固定为 v0.5.0；
- v0.3.0 为更多双拼方案，v0.4.0 为高级功能；
- 后续发布压缩包、文件夹和开发计划文件必须带版本号；
- 样例评估结果不得描述为真实引擎准确率。

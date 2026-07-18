# 候选质量指标与报告格式

## 1. 引擎结果输入

每条结果至少包含：

```json
{
  "id": "LM-LONG-DAILY-001",
  "candidates": ["我今天下午要去超市买点水果。"],
  "committed_text": "我今天下午要去超市买点水果。",
  "candidate_latency_ms": 18
}
```

`id` 必须与 `tests/` 中的稳定测试 ID 对应。

## 2. 核心指标

- **Top-1**：目标文本是否位于候选第 1 名；
- **Top-5**：目标文本是否位于候选前 5 名；
- **Target Rank**：目标首次出现的位置，未出现记为 null；
- **Exact Match**：最终上屏文本是否与目标完全一致；
- **Character Error Rate（CER）**：目标文本与上屏文本的字符级编辑距离除以目标字符数；
- **Latency P50/P95**：候选出现延迟的中位数和 95 分位数。

## 3. 使用方法

```bash
python tools/evaluate_candidate_results.py reports/engine_results.json --out generated/evaluation_report.json
```

## 4. 注意事项

- `reports/sample_engine_results.json` 只是格式演示，不代表 Lite IME 当前真实准确率；
- 候选列表应保持引擎原始顺序，不得在适配器中预先排序；
- 延迟计时起点和终点必须在同一测试环境中固定；
- 不同硬件、平台和冷/热启动结果不得直接混合统计；
- 平台性能矩阵将在 v0.5.0 完整建立。

# PiInput Shift 中英文切换恢复设计

## 目标

单独按下并释放左 Shift 或右 Shift，必须在中文与英文状态之间切换。英文候选是否开启，只控制英文候选窗，不得控制英文状态是否存在。

## 已确认根因

1. `HostSession` 只在 `[english].enabled=true` 时创建 `EnglishSession`，并在没有 `EnglishSession` 时拒绝 `switch_to_english`。当前用户设置没有 `[english]` 段，因而使用默认值 `enabled=false`，Shift 切换被 Host 拒绝。
2. `ShiftToggleState` 只覆盖完整收到 KeyDown/KeyUp 的路径。某些 TSF 宿主漏发 Shift KeyUp 后，状态机会一直认为 Shift 仍按下。

## 行为

- `[english].enabled=false`：Shift 仍切换到英文；字母与 ASCII 标点逐键直接提交，不显示英文候选。
- `[english].enabled=true`：Shift 切换到英文候选模式，继续使用现有离线英文词库与学习逻辑。
- 完整收到 Shift KeyUp 时立即切换。
- 漏掉 Shift KeyUp 时，下一次普通按键到来且物理 Shift 已松开，先补做一次切换，再处理该按键。
- Shift 仍处于按下状态时到来的字母、数字或标点只把 Shift 标记为修饰键，不切换。
- 焦点切换、停用输入法和新的 Context 绑定继续清空未完成的 Shift 状态。

## 范围

本次只修复 Shift。中文符号映射将基于搜狗、微信和 PiInput 的 42 个主键盘符号对照表单独设计，不与本次改动混合。

## 验收

- 默认设置下：中文 → Shift → `book_list` 直接输入英文 → Shift → 中文输入。
- 英文候选开启时，两次 Shift 往返并保持英文候选。
- 漏 KeyUp、重复 KeyDown、Shift+字母、Shift+数字、Shift+标点、左右 Shift 均有自动回归。
- Release 构建和完整 CTest 全部通过。

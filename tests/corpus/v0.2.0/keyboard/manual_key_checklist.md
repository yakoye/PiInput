# 键盘交互测试清单 v0.1.0

静态文本只能验证可打印字符。以下按键必须在真实输入框中逐项操作。

## 1. 编辑键

- [ ] Backspace：删除组合串最后一个编码；删空后候选窗口关闭。
- [ ] Delete：删除光标后的编码或正文字符。
- [ ] ArrowLeft / ArrowRight：在组合串中移动并可中间编辑。
- [ ] ArrowUp / ArrowDown：按产品定义移动候选或移动正文光标。
- [ ] Home / End：按控件和产品规则移动光标。
- [ ] PageUp / PageDown：候选翻页时不得输入意外字符。
- [ ] Insert：不得导致输入法状态异常。

## 2. 确认和取消

- [ ] Space：选择首候选；无候选时输入空格。
- [ ] Enter：按产品规则确认候选、提交原始编码或换行。
- [ ] Escape：取消组合且不得意外上屏。
- [ ] Tab：按产品规则切换候选、焦点或缩进。

## 3. 修饰键

- [ ] Left Shift / Right Shift 分别测试。
- [ ] Left Control / Right Control 分别测试。
- [ ] Left Alt / Right Alt 分别测试。
- [ ] Meta 键不应被输入法无条件拦截。
- [ ] Caps Lock 状态切换后英文大小写正确。

## 4. 功能键和扩展键

- [ ] F1–F12 不应被错误上屏或吞掉。
- [ ] Print Screen、Scroll Lock、Pause 不影响组合状态。
- [ ] Num Lock 开关状态下数字小键盘行为明确。
- [ ] Numpad Enter 与主 Enter 行为不产生重复提交。

## 5. 当前边界

Windows、macOS、Apple/Windows 外接键盘、ANSI/ISO/JIS、Fn/Globe 和系统快捷键冲突的完整矩阵在 v0.5.0 实现。v0.1.0 只验证通用逻辑动作。

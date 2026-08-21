# PiInput v0.4.6 真实应用验证记录

测试时间：2026-08-13 09:18～09:36（Asia/Shanghai）  
完整报告：`artifacts/test-reports/20260813-093620-real-app-acceptance.md`  
验收规范：`docs/superpowers/specs/2026-08-13-piinput-real-application-acceptance-design.md`

## 环境

- 系统实际 Host：`0.4.6-20260813-011803-16488`；
- 协议版本：`2`；
- 测试后 Host PID：`31408`；
- Host 工作集：`46.46 MB`；
- 发布 ZIP SHA-256：`675E9710992BB1B6EF72350EFB32F2FF0B0B2F0093FB146F99CC37EF7B83F7E3`。

## 自动测试

```text
100% tests passed, 0 tests failed out of 51
Total Test time (real) = 191.93 sec
```

## 真实应用矩阵

| 应用 | 状态 | 结果 |
|---|---|---|
| Notepad4 | `PASS` | 首键、Space、完整长候选、分段取字、符号和 ASCII 标点通过 |
| 文件资源管理器 | `PASS` | Host 冷启动、重命名候选、Space 与 Enter 通过 |
| Claude | `FAIL` | 底部输入框候选向下展开并超出屏幕 |
| Notepad++ | `BLOCKED` | 多实例/双编辑区无法稳定选择安全空白区，停止以免修改用户文档 |
| Chrome | `BLOCKED` | Chrome 专用控制连接不可用 |
| Codex/ChatGPT | `BLOCKED` | 不自动控制当前 Codex/ChatGPT 应用自身 |
| Windows 记事本 | `BLOCKED` | 未返回可控制窗口 |
| 其他扩展应用 | `NOT RUN` | 本轮没有安全、唯一的空白测试窗口 |

## 已验证行为

### 候选不省略

`wouuru` 的六个候选完整显示；`wogjjthfhcys` 的六个长候选也完整显示，没有 `…`。Space 可正常确认。

### 分段取字

`fwihkk` 首屏显示整词候选。按 `=` 后进入“非常、非、飞、费、肥、菲”；选择“非”后，`非` 留在 Composition，继续为 `chang'kuai` 提供“畅快、长、唱、常、厂、场”。没有提前上屏或生成多行假句子。

### 符号和程序员标点

- `;sheshidu` → `℃`；
- 中文状态下 `++ -- == 1.9` 保持 ASCII；
- Shift 作为 `+` 修饰键没有造成可见误切换。

### 资源管理器 Host 冷启动

- 首键调用：`165 ms`；
- 首键到截图确认候选：`687 ms`，其中包含截图采集开销；
- `wo` 首选“我”；
- 候选紧贴重命名框；
- Space 和 Enter 正常完成 `我.txt` 重命名。

## 已确认缺陷

`CLAUDE-CANDIDATE-WORKAREA-001`：当 caret 位于屏幕底部时，候选窗没有翻到上方，绝大部分落在屏幕外。修复后必须增加工作区边界自动测试和 Claude/底部文本框人工复测。

## 人工复核边界

- 自动按键工具不能证明单独 Shift 产生了与实体键盘完全一致的 key-down/key-up 序列，因此单独 Shift 仍需用户实体键盘复测；
- Notepad++、Chrome、Codex/ChatGPT 的阻塞项不得写成通过；
- 拆字模式尚未实现时为 `N/A`；
- 125%、150%、200% DPI 和多显示器仍需专门验收。

## 结论

v0.4.6 的 51 项自动测试全部通过，Notepad4 与资源管理器核心输入链路通过；但 Claude 候选越界属于真实兼容缺陷，因此当前不能标记为“所有真实应用验收通过”。


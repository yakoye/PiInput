# PiInput v0.1.6-dev 下一步开发计划

## 第一优先级：完成 TSF 真机运行闭环

根据 v0.1.5 的 Windows 输出继续处理：

- 剩余 MSVC/Windows SDK 编译错误；
- DLL 导出和 `regsvr32`；
- COM 当前用户注册；
- TSF profile 激活；
- Win+Space 可见性；
- 记事本 Composition；
- 候选窗口；
- Space/数字键上屏；
- Esc、Backspace 和方向键。

## 第二优先级：中英文状态

- Shift 或可配置快捷键切换；
- 中文/英文状态持久化；
- Composition 存在时的 Shift 行为；
- 应用切换后的状态规则；
- 状态提示。

## 第三优先级：标点接入 TSF

- 中文标点；
- 英文标点；
- 程序员标点；
- 中文输入中临时 ASCII 标点；
- 不同应用的标点状态基础。

## 第四优先级：候选窗口定位

- 通过 TSF context view/text extent 获取插入点；
- caret 方案只作为回退；
- 高 DPI；
- 多显示器边界；
- 候选窗不抢焦点；
- 应用关闭后的生命周期。

## 第五优先级：应用兼容记录

建立第一轮真机表格：

```text
记事本
Visual Studio
VS Code
Chrome
Edge
微信
Windows Terminal
```

## 本阶段暂不开始

- 移动端；
- 跨设备同步；
- AI；
- 语音；
- 正式签名安装器。

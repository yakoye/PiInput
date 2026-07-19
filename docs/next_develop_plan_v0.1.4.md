# PiInput v0.1.4-dev 下一步开发计划

## 主目标

建立 Windows TSF 最小可用输入法链路，使 PiInput 能够出现在 Windows 输入法列表，并在普通文本框中完成中文候选上屏。

## 任务顺序

### 1. TSF 组件基线

- 建立 `platform/windows/tsf`；
- 实现 COM DLL 生命周期；
- 实现 `ITfTextInputProcessorEx`；
- 实现 `ITfKeyEventSink`；
- 建立 CLSID、Profile GUID 和语言配置；
- 注册、注销和开发版升级脚本；
- 构建 x64 Debug/Release。

### 2. Composition

- 接收 a-z、Backspace、Delete、左右键、Home、End；
- 建立和更新 TSF Composition；
- 将光标编辑映射到现有 `ImeSession`；
- Enter 上屏原始输入；
- Esc 取消；
- 中英文切换。

### 3. 候选窗口

- 使用现有候选快照；
- Space 选择第一候选；
- 数字键选择；
- `-`/`=` 或 PageUp/PageDown 翻页；
- 候选窗口跟随插入点；
- 高 DPI 和多显示器；
- 已显示候选绝不异步跳变。

### 4. 标点和符号接入

- 中文/英文/程序员标点状态；
- 中文输入中临时 ASCII 标点；
- `;` 符号搜索进入候选栏；
- 符号上屏后返回普通输入状态。

### 5. Windows 真机验证

至少验证：

- 记事本；
- Visual Studio；
- VS Code；
- Chrome/Edge；
- Windows Terminal；
- 微信；
- 文件资源管理器重命名。

## 发布门槛

- Windows 本机构建无警告级错误；
- 可安装、可卸载、可重新安装；
- 发生异常时不使宿主应用崩溃；
- 候选快照稳定性测试通过；
- 用户数据升级保留；
- 完整验证记录进入发布包。

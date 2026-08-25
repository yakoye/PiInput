# PiInput v0.7.15 版本说明

v0.7.15 汇总 v0.7.12 之后完成的输入可靠性、Windows 系统宿主、智能标点、工具候选和发布工程改进。

## 输入与候选可靠性

- 修复 Chrome、远程桌面等新输入框首个数字可能被 Resume 请求误当作选词键的问题。
- Windows 搜索使用 TSF UIElement 集成候选；普通桌面程序继续使用外部候选窗。
- 中文组合中输入 `cmd` 后单按 Shift，会先原样提交 `cmd` 再切到英文，不再丢失组合字母。
- 13 个专业词缺口已补齐；大词库使用只读内存映射，避免重复堆内存副本。

## 中文标点

- 设置简化为“中文标点 / 英文标点”；旧“程序员标点”按英文标点迁移。
- 中文标点下，数字后的第一下句号为 `.`，第二下为 `。`；`1.文本` 不再异步变成 `1。文本`。
- `12:23` 保持英文冒号，中文正文仍可输入中文冒号；`2/3` 的斜杠保持 ASCII。
- 分号恢复标点本义，旧 `;;f`、`;;u` 和 `;关键词` 命令已删除。

## 日期、符号和工具入口

- `sj/shij/shijian` 可展开时间，`riq/riqi` 可展开日期。
- `fh/fuhao/fuh/fuhc`、`bq/biaoqing/biaoq/bnqk/bnq`、`shizhi/uevi/sz/shiz/uev` 的候选 2 分别打开符号、表情和设置。
- `jisuanqi/jisrqi/jsq/jisrq/calc/reg` 的候选 2 打开 Windows 计算器，候选 3 打开随包程序员计算器。
- `hxtu/ht/huatu/mspaint/msp` 的候选 2 打开 Windows 画图。“快捷/kuaijie”不是触发码。
- 设置新增 3 组自定义快捷调用，可设置多个字母触发码、候选位置、名称以及 EXE、HTML、文件、URL 或 `cmd:` 命令目标。
- `RegCalc64Tool.html`、`shared-ui.css`、`shared-ui.js` 随包直接安装到活动版本 `bin`。

## 安装与诊断

- 普通用户进程负责用户文件、设置和 HKCU；只有机器 TSF profile/category 与 HKLM COM 使用窄范围 UAC 子步骤。
- 安装、卸载结果窗口置顶；安装成功后打开 UserData 和设置程序。
- “关于 PiInput”显示版本、精确 build ID、UTC 构建时间、完整 Git Commit ID 和联系方式。

## 发布边界

发布资产必须与冻结提交、build ID、测试记录和 SHA-256 一致。代码签名、安装升级卸载、8 小时稳定性和真实宿主矩阵的实际结果以同版本验证记录为准；未运行项不得由自动单元测试替代。

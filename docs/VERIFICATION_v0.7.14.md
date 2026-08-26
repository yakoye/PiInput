# PiInput v0.7.14 验证记录

验证日期：2026-08-25

<!-- release-gates
host_soak_8h=NOT_RUN
tsf_app_soak_8h=NOT_RUN
p0_real_host_matrix=NOT_RUN
-->

## 本版自动验证范围

- 中文输入模式与英文标点模式相互独立：中文候选保持可用，Host 按完整 ASCII 标点表提交。
- TSF 智能标点仅在组合边界读取标点策略；英文标点模式旁路 provisional 规则，旧 `programmer` 值只作为迁移别名。
- 设置解析、设置页选项、Host 组合提交、源码约束、完整键盘标点表和源码清单均有自动回归。
- 分号本义以及符号、表情、设置候选 2 的组件与协议回归继续保留。
- “关于 PiInput”的构建标识、UTC 构建时间、完整 Git Commit ID 和项目联系方式由 CMake 构建元数据注入，并由 Windows 源码门禁固定字段。
- 中文组合中输入 `cmd` 后单按 Shift，Host 以一次 commit 返回原始 `cmd` 并进入英文模式；英文候选开启和关闭两条路径均纳入会话回归。
- 符号、表情和设置候选 2 的动作协议保持不提交标签；TSF 启动路径改为从 `CurrentHostPath` 定位当前 Host 同目录程序，并加入路径单元回归和源码门禁。真实点击启动仍需安装后手工验收。
- 计算器六组别名固定提供候选 2 的 Windows 计算器和候选 3 的随包程序员计算器；画图五组别名固定提供候选 2 的 Windows 画图。“快捷/kuaijie”明确加入负向回归，不产生启动动作。
- 自定义快捷调用覆盖 3 个设置槽、配置解析与往返保存、候选位置、动作目标协议、Host 组合清理和 TSF 编辑完成后启动顺序；空目标不会清除用户组合。
- `RegCalc64Tool.html` 及其 CSS/JS 作为运行时资产直接进入 `bin`，由 CMake 安装、发布白名单和静态包闭环共同检查。

## 本版仍需人工完成

- 设置窗口的实际布局和选项可见性，需要安装本包后由用户目视确认；自动化不会抢占用户桌面。
- Windows 计算器、画图、随包 HTML 以及用户自定义目标的实际拉起，需要安装同一候选包后手工验收；自动测试只验证候选、协议、路径解析和调用时序，不伪装成桌面验收。
- Windows 搜索、Notepad++、浏览器、Office 等真实宿主仍需加载本版同一 DLL 后验收。
- 本地没有可信代码签名证书，本包不得作为正式签名发行版。
- v0.7.14 修改了实现，旧版 8 小时结果不能替代本版 Host/TSF/App 稳定性门禁。

## 包闭环记录

冻结实现提交：`dd63bb9837875fe47cd1539d25c5da4f4e6e5413`。

- 精确 build ID：`0.7.14+dd63bb983787`；
- 提交后重新构建的 Release 回归：66/66 PASS；
- ZIP：工作区 `releases/history/v0.7.14/PiInput-v0.7.14-windows-x64.zip`，38 个文件，22.17 MB；
- SHA-256：`ffe4d28dc0333ce54fd4ade0054dd66a7597b2985591fea7bf7adcf63e6ed9f3`；
- 静态包闭环：PASS，报告位于 `artifacts/package-closure/history/package-closure-v0.7.14-dd63bb983787/summary.json`；
- 包内 `bin` 已核对 `RegCalc64Tool.html`（161403 bytes）、`shared-ui.css`（31016 bytes）和 `shared-ui.js`（2468 bytes）。

本次没有运行安装冒烟、升级/卸载闭环、Controlled TSF 真实 profile 或桌面程序启动，因此 `install_closure=not-run`，签名仍为未要求的本地开发策略。上述静态 PASS 不代表安装闭环或正式签名发布通过。

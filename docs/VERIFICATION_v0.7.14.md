# PiInput v0.7.14 验证记录

验证日期：2026-08-25

<!-- release-gates
host_soak_8h=NOT_RUN
tsf_app_soak_8h=NOT_RUN
p0_real_host_matrix=NOT_RUN
-->

## 本版自动验证范围

- 中文输入模式与英文标点模式相互独立：中文候选保持可用，Host 按完整 ASCII 标点表提交。
- TSF 智能标点仅在组合边界读取标点策略；English/Programmer 标点模式旁路 provisional 规则。
- 设置解析、设置页选项、Host 组合提交、源码约束、完整键盘标点表和源码清单均有自动回归。
- 分号本义以及符号、表情、设置候选 2 的组件与协议回归继续保留。
- “关于 PiInput”的构建标识、UTC 构建时间、完整 Git Commit ID 和项目联系方式由 CMake 构建元数据注入，并由 Windows 源码门禁固定字段。
- 中文组合中输入 `cmd` 后单按 Shift，Host 以一次 commit 返回原始 `cmd` 并进入英文模式；英文候选开启和关闭两条路径均纳入会话回归。

## 本版仍需人工完成

- 设置窗口的实际布局和选项可见性，需要安装本包后由用户目视确认；自动化不会抢占用户桌面。
- Windows 搜索、Notepad++、浏览器、Office 等真实宿主仍需加载本版同一 DLL 后验收。
- 本地没有可信代码签名证书，本包不得作为正式签名发行版。
- v0.7.14 修改了实现，旧版 8 小时结果不能替代本版 Host/TSF/App 稳定性门禁。

## 包闭环记录

打包完成后以包内 `PiInputHost.exe --version`、`--build-id`、ZIP SHA-256 sidecar、运行时白名单和静态包闭环报告为准。未执行安装冒烟时，不得把静态包校验写成安装闭环通过。

# PiInput v0.4.6-dev 验证记录

验证环境：Windows x64、Visual Studio 18 Build Tools、Windows SDK 10.0.26100.0。

## 已建立的回归

- 候选长度驱动 6/2/1 列布局，长句完整显示；
- 同一组合锚点固定、宽度单调增长；
- 不同文本 Context 重置 session、sequence、raw、caret 和 generation；
- 旧 session 的迟到回复被拒绝；
- Pending Context 同时校验 session id 和 sequence；
- Shift 缺失回调 Context 时使用当前焦点；
- update/commit/cancel 的异步 TSF 编辑回退；
- 中文 `-`、`=`、`+` ASCII 策略及数字后小数点策略；
- Host 冷/热连接、候选导航、分段取字和 Composition mirror。

## 最终自动验证

- Windows Release 全目标单并发构建：通过；
- 完整 CTest：`51/51` 通过，`0` 失败，用时 `208.46 s`；
- Host 冷/热连接、20 次重启交接与外部增量性能：通过；
- 407 个全拼音节、406 个用户确认小鹤码、786 条结构化语料：通过；
- 3500/7000 汉字覆盖、真实 SCEL、外部大词库与段落输入：通过；
- 安装器、卸载器、迁移、品牌、发布元数据与 SHA-256 门禁：通过；
- CMake 安装布局和 Windows x64 发布 ZIP：通过。

## 人工验证边界

本轮只生成用户安装包，不自动安装、不结束用户应用、不推送仓库。Notepad4、Notepad++、资源管理器、Chrome、Codex 等真实宿主的最终结果由用户安装本包后验证。

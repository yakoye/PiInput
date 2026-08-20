# PiInput v0.4.7-dev 验证记录

验证环境：Windows x64、Visual Studio 18 Build Tools、Windows SDK 10.0.26100.0。

## 回归覆盖

- 正常 Shift KeyDown + KeyUp 立即切换；
- Shift 作为修饰键时不切换；
- Shift KeyUp 丢失后，下一个未按住 Shift 的普通键触发恢复切换；
- 恢复后迟到的 KeyUp 不重复切换；
- 英文候选关闭时仍可进入英文直输；
- 英文直输字母与英文标点直接提交；
- 英文直输状态跨 Host 重启恢复；
- Windows 稳定 TSF 入口实际接入恢复状态机。

## 定向验证

- `piinput-core-tests`：通过；
- `piinput-host-session`：通过；
- `piinput-windows-source-regression`：通过。

## 最终验证

- Windows Release 全目标单并发构建：通过，退出码 `0`；
- 完整 CTest：`51/51` 通过，`0` 失败，用时 `217.42 s`；
- Host 冷/热连接、20 次重启交接与外部增量性能：通过；
- 407 个全拼音节、406 个用户确认小鹤码、786 条结构化语料：通过；
- 3500/7000 汉字覆盖、真实 SCEL、外部大词库与段落输入：通过；
- 安装器、卸载器、迁移、品牌、发布元数据和 Windows 稳定入口源码门禁：通过；
- SHA-256 完整性、CMake 安装布局和最终 Windows x64 发布 ZIP：通过。

## 人工验证边界

本轮不自动安装、不结束用户应用、不推送仓库。真实宿主是否漏发 Shift KeyUp 只能由用户安装本包后在目标应用中验证；自动测试已覆盖相同状态转换及下一键恢复行为。

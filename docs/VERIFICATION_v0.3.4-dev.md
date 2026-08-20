# PiInput v0.3.4-dev 验证记录

本记录由本轮 Windows x64 Release 构建生成。自动化结果和人工验收分开记录，未通过人工输入验收前不宣称系统输入链路已经完成。

## 自动化范围

- 新 TSF CLSID/Profile GUID 精确标识；
- 当前用户键盘注册字符串；
- 安装器旧身份清理路径；
- 独立输入测试台发布布局；
- 全拼、小鹤双拼、词库、候选、增量解码、英文候选和安装安全回归；
- Release 包结构与 SHA-256。

## 人工验收

待安装后验证：

1. 双击 `PiInput-Test.exe`，测试 `gjjt`、`jpiu`、`cihv`、`xnhe`；
2. 双击 `PiInput-Install.exe`；
3. 新开一个记事本，通过 `Win+Space` 选择 PiInput；
4. 确认能够组成候选、空格上屏、`=` 展开多行；
5. 确认切换输入法时不再出现旧版初始化卡顿。

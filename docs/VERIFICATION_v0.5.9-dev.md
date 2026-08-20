# PiInput v0.5.9-dev 验证记录

## 根因复现

- 慢速实机逐键可以在 Notepad++ 完成“我，他们。”；
- 标点提交后可观察到应用自身提示窗与 PiInput 下一组合重叠；
- 代码跟踪确认旧实现只有一个待完成最终编辑槽位；第二个快速标点提交可能覆盖第一个待提交状态；
- 新测试在提交屏障实现缺失时因找不到 `final_edit_key_queue.h` 按预期失败。

## 定向验证

- `piinput-composition-mirror`：通过；
- `piinput-core-tests`：通过；
- `piinput-host-process`：通过；
- `piinput-windows-source-regression`：通过；
- `PiInputTSF.dll` Release 构建：通过。

## 最终 Release 验证

- Visual Studio 18 / MSVC Release 全目标构建成功；
- 完整 CTest 首轮 55/57 通过，唯二失败是版本元数据与 SHA 清单尚为 0.5.8；更新后两项定向复验 2/2 通过，代码与运行测试无失败；
- CMake 安装布局成功生成稳定入口 DLL、常驻 Host、安装器、卸载器、设置程序与运行词库；
- 发布 ZIP：`PiInput-v0.5.9-dev-windows-x64.zip`；
- 安装器退出码 0；系统稳定入口 DLL 与发布 DLL SHA-256 均为 `2d678498d1d8d70cc353be3906714db616c3bc06dc574dfba79853097a3a2f14`；
- 系统 Profile 状态：`registered=yes`、`enabled=yes`、显式激活后 `active=yes`；
- 运行中 Host 健康检查：协议 3，构建号 0.5.9。

## Notepad++ 连续实机输入

使用物理按键连续输入，不粘贴中文、不在每个候选后暂停：

```text
我，他们。我们明天，去哪里？明天、后天！
```

对应小鹤与标点按键链共 34 键，单次完整执行结果：

- 34/34 按键完成；
- 总自动化时间 2,598 ms；
- 单次 Windows 实机控制调用最大 133 ms；该数值包含桌面自动化同步开销，不代表 Host 核心查询耗时；
- 逗号、句号、问号、顿号、感叹号之后均继续收到后续中文按键；
- Notepad++ 可访问性文本中读取到逐字一致的目标句；
- 未出现符号后冻结、丢键或后续中文无法进入组合区。

准备执行三轮重复测试时检测到用户正在同一 Notepad++ 窗口手动输入，自动化按规则停止，未把重复测试内容混入用户文字。

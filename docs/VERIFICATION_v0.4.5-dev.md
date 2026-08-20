# PiInput v0.4.5-dev 验证记录

验证环境：Windows x64、MSVC 19.51、Visual Studio 18 Build Tools、Windows SDK 10.0.26100.0。

## RED / GREEN

- RED：输入 `w` 后继续输入 `o`，候选窗口跟随新 caret 水平移动；
- GREEN：同一组合普通更新完全保持初始矩形，显式展开只改变高度；
- RED：窗口重新获焦时 Resume 没有关联 `ITfContext`，回复被丢弃；
- GREEN：通过当前文档顶层 Context 保存 Resume sequence，并在该 Context 恢复组合；
- RED：Host 已清空候选后，宿主拒绝同步提交会吞掉 Space/Enter；
- GREEN：失败保留 mirror 状态并恢复 Host；同步提交不允许时使用完成感知的异步写入；
- GREEN：另一应用会话开始前重置旧候选几何，鼠标临时锚点只允许被真实文本 caret 校正一次。

## 自动验证

- Windows Release 全目标单并发构建：通过；
- 候选几何、焦点协议、Composition mirror、Host Session 和 Windows 源码门禁：通过；
- 第一轮完整 CTest：功能项 `50/50` 通过，唯一预期失败为修改后尚未刷新的 SHA-256 清单；
- 最终完整 CTest：`51/51` 通过，`0` 失败，实际用时 `170.03 s`；
- Host 冷/热启动、重启交接、外部大词库、段落输入、3500/7000 汉字覆盖：通过；
- CMake 安装布局与 Windows x64 发布 ZIP 生成：通过；
- ZIP SHA-256 以发布目录中的同名 `.sha256.txt` 伴随文件为准。

## 人工验证边界

本包不会由构建代理自动安装到当前系统。Codex、Notepad4 等真实应用的最终行为需用户安装本包后验证，避免构建过程影响当前输入环境。

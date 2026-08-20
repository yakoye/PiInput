# PiInput 组合取消与失焦恢复实施计划

1. 为 CompositionMirror 添加外部终止和旧回复失效测试。
2. 为 SessionManager 添加精确会话取消测试。
3. 为候选右键菜单命令映射添加取消测试。
4. 修复 `OnSetFocus`：保留在途编辑，只在真实断线时恢复。
5. 修复 `OnCompositionTerminated`：清镜像、拒绝旧回复、取消 HostSession。
6. 增加 Host 到稳定入口 DLL 的精确取消通知。
7. 隐藏无候选空白窗，增加 `fdsafds` 回归。
8. 增加 `mktm → 明天 → = → 明` 回归。
9. 运行 Windows Release 全构建、完整 CTest、安装布局和发布包校验。

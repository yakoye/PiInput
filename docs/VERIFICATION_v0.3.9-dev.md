# PiInput v0.3.9-dev 验证记录

验证日期：2026-08-11  
平台：Windows x64、MSVC 19.51、Windows SDK 10.0.26100.0  
配置：Release

## 构建

- CMake 全目标 Release 构建成功；
- `PiInputTSF.dll`、`PiInput-Install.exe`、`PiInput-Uninstall.exe`、`PiInput-Test.exe` 及所有命令行工具均生成；
- CMake 安装布局包含英文词频库、英文补充词、英文前缀偏好表、完整中文二进制词库和符号表。

## 自动测试

```text
34/34 tests passed
0 tests failed
Total Test time: 127.20 sec
```

覆盖范围包括：

- 全拼、小鹤双拼、合法音节、未完成编码、长句和 3500/7000 汉字覆盖；
- 候选网格、分段取字、符号与标点；
- 外部大型词库性能与真实 SCEL；
- 英文词库、用户学习、TSF 英文状态机和严格首屏顺序；
- 安装布局、原生卸载器、失败关闭、TSF 注册源代码门禁；
- 品牌过滤、CRLF 命令入口、发布元数据和 SHA-256 文件清单。

英文首屏顺序回归实际通过：

```text
r     → r, right, really
re    → re, really, remember
rev   → rev, review, reverse
reve  → reve, revile, reverse
b     → b, but, because
bo    → bo, both
boo   → boo, book, boom
book  → book, books, booked
```

## 发布包

```text
artifacts/PiInput-v0.3.9-dev-windows-x64.zip
SHA-256: 5c10b1e96be325731114cbcaf19b9df713ce28dfc59dbf095099a9aacfadca29
```

最终 ZIP 已重新解压并检查 12 个必须文件，包括根目录和 `bin` 内的原生卸载器、安装器、TSF DLL、完整中文词库、三个英文数据文件、符号表、查询脚本与安装使用指南。包内 `query-dictionary.cmd` 为 CRLF 换行。

## 未执行的破坏性系统操作

本轮没有自动安装或卸载用户当前系统中的输入法。真实 TSF 切换、Windows“已安装的应用”入口、卸载时的 UAC/对话框及任务栏 `π` 图标，需要用户从最终 ZIP 手动安装后验证。

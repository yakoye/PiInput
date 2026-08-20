# PiInput v0.3.8-dev 验证记录

验证日期：2026-08-11  
验证平台：Windows x64、Visual Studio 18 2026、Windows SDK 10.0.26100.0

## 问题复现

修复前，真实启动 `query-dictionary.cmd` 会稳定输出：

```text
'65001' is not recognized as an internal or external command
'-NoProfile' is not recognized as an internal or external command
'1' is not recognized as an internal or external command
'词库查询失败。' is not recognized as an internal or external command
```

文件检查确认批处理使用纯 LF 换行；Windows `cmd.exe` 因此丢失每行第一个命令词。

## 修复与回归测试

- `.cmd` 和 `.bat` 由 `.gitattributes` 固定为 CRLF。
- `query-dictionary.cmd` 已重新规范为 UTF-8、CRLF。
- 新增 `piinput-query-dictionary-cmd-regression`，通过真实 `cmd.exe` 启动该入口。
- 测试先在旧文件上按预期失败，规范换行后通过。

## 完整验证

- Release 全目标构建：通过。
- CTest：32/32 通过，0 失败，总耗时 68.93 秒。
- 发布 ZIP 解压后再次实际启动 `query-dictionary.cmd`：退出代码 0。
- 发布文件换行统计：10 个 LF，全部 10 个均有对应 CR，即不存在裸 LF。
- 发布工具输出中 `is not recognized as an internal or external command`：0 次。

## 发布包

```text
artifacts/PiInput-v0.3.8-dev-windows-x64.zip
SHA-256: 3b2fb64a090bf2d077f4b940aba17874d1ca2a661b64c1e370999f91d9ef295a
```

本轮没有自动安装、没有提交、没有推送仓库。正式卸载程序尚未加入本版。

# PiInput v0.3.8-dev 版本说明

## 修复

- 修复双击 `query-dictionary.cmd` 后，Windows `cmd.exe` 将 `chcp`、`powershell.exe`、`if errorlevel` 和中文提示的首个词吞掉的问题。
- 根因是批处理文件使用纯 LF 换行；现在仓库通过 `.gitattributes` 强制所有 `.cmd` 和 `.bat` 使用 Windows CRLF。
- 新增真实启动 `cmd.exe` 的自动回归测试，确保发布入口不再产生 `is not recognized as an internal or external command`。

## 延续 v0.3.7 的内容

- 24,323 条离线英文高频词及英文补充词表。
- 英文逐键补全、原始输入置首、精确前缀优先和有限模糊补全。
- 501,935 条中文词库、全拼、小鹤双拼、长词命中与分段取字。

## 未包含

- 本版尚未加入正式卸载程序；该功能将在卸载行为确认后单独实现。
- 本轮不自动安装、不提交、不推送仓库。

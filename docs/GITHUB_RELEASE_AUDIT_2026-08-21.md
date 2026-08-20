# GitHub 发布审计（2026-08-21）

目标仓库：`yakoye/PiInput`

本次发布范围从 `PiInput-v0.3.0-dev-windows-x64.zip` 到
`PiInput-v0.7.11-windows-x64.zip`。发布前确认 GitHub 仓库没有已有 tag 或
Release，因此不会覆盖既有发布记录。

## v0.7.11 发布门禁

- `VERSION`：`0.7.11`
- Windows x64 Release 完整 CTest：61/61 通过
- ZIP 可完整读取：38 个条目
- ZIP SHA-256：`F65F7A99AE9E1FFDE15ED94AB53E506819F74C4E7A04F4424555D96AA8AC57BA`
- 发布包：`PiInput-v0.7.11-windows-x64.zip`

## 历史资料边界

- `artifacts/old_packages` 保存 v0.3.0-dev 至 v0.7.1 的可用历史运行包；
  v0.7.2 至 v0.7.11 的运行包位于 `artifacts`。
- v0.3.2 的唯一包位于 `withdrawn/v0.3.2-unsafe`，不作为正常版本发布。
- v0.6.7 没有找到运行包，不伪造 Release 资产。
- 历史运行包不包含源码。可核验的 v0.3.0 源码边界为提交 `4a41faa`；
  v0.7.11 完整源码、测试和发布文档随本次提交公开。

## 发布约定

- tag 使用版本号（例如 `v0.7.11`、`v0.6.9-dev`）。
- Release 标题使用对应 ZIP 文件名去掉 `.zip` 后的完整名称。
- Release 正文优先使用仓库内对应 `docs/release_notes_*.md`。

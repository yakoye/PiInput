# PiInput v0.4.2-dev 验证记录

本文件记录 Windows x64 开发测试包的可重复验证结果。

## 已完成的定向验证

- Host 协议 v1/v2 兼容、消息往返、活动候选列和分段组合文本；
- CandidateGrid 单行默认、横向/纵向移动和分段单字高亮；
- HostSession 重复 `=` 进入分段取字并最终一次提交；
- CompositionMirror 分段文本显示与 Host 重启原始编码恢复；
- 常驻 Host 第二客户端热请求和连续 20 次 Host 重启恢复；
- Stable Runtime 当前 Host 回退解析和登录启动命令；
- Windows 源码门禁：Per-Monitor-V2、同一编辑会话 caret、Shift 状态机、四向候选导航、并行入口修订。

## 发布验证结果

验证日期：2026-08-12（Asia/Shanghai）。

```text
Windows Release 全目标构建：成功
CTest：50/50 通过，0 失败
CTest 实际耗时：137.35 秒
Host 进程：第二个客户端热请求门禁通过
Host 重启：20/20 次组合恢复通过
真实外部增量性能：通过
真实 SCEL/外部词库：通过
长段落输入：通过
3500/7000 汉字覆盖：通过
SHA256SUMS 源码完整性：通过
```

安装布局确认包含：

```text
PiInput-Install.exe
PiInput-Uninstall.exe
PiInput-Test.exe
bin/PiInputTSF.dll
bin/PiInputHost.exe
bin/piinput-diagnostics.exe
data/piinput-base.lex
data/host_protocol.json（协议 v2，兼容 v1）
```

最终用户包：

```text
artifacts/PiInput-v0.4.2-dev-windows-x64.zip
```

SHA-256 以压缩包同目录的 `.sha256.txt` 为准，避免把压缩包自身哈希写回包内造成递归变化。

本轮只生成并验证安装包，未自动安装到系统，也未切换用户当前输入法。

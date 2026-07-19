# 新会话续接说明

## 必读

依次阅读 `PROJECT_CONTEXT.md`、`README.md`、最新版本说明与验证记录、`docs/TSF_DEVELOPER_TEST.md`、`docs/词库更新说明.md`，以及 `docs/superpowers/plans/` 下最新计划。不要只根据聊天摘要猜测状态。

## 当前基线

- 分支：`feat/v0.2.0-dictionary-foundation`；
- 版本：`v0.2.0-dev`；
- Windows x64 TSF 已能编译、注册和输入；
- 本地开源词库可构建到约 45.9 万词条；
- 候选横向显示，单字 9 项、词语 6 项，`-`/`=` 翻页，单独 Shift 切中英文；
- 常用词门禁包括 `接触`、`词汇`、`感觉`、`现在`、`中国`；
- 增量候选包括小鹤 `mkt→明天`、`rug→如果/入股` 与全拼尾音节前缀；
- 中文标点已经接入 TSF；
- 开发安装器 `PiInput-Install.exe` 使用版本并存，不覆盖占用中的 DLL，不关闭用户应用；
- 407 音节、786 条结构化语料已纳入 `tests/corpus/v0.2.0`。

## 本地目录

```text
C:\Users\color\Downloads\piinput
├── PiInput-repo
└── dicts
```

源码包和 Git 不包含外部大型词库。`dicts`、SCEL 和用户数据均不得在源码更新时删除。

## 常用入口

```text
setup-dev.cmd            构建、测试、并存安装
update-dictionaries.cmd  下载、转换、构建外部词库
run-ime-tests.cmd        输入法完整回归
dist\windows-x64\bin\PiInput-Install.exe  只安装当前构建
```

## 下一阶段边界

继续优先完善全拼、小鹤、词库命中率、长句切分、候选稳定性和延迟。语料中的纠错、模糊音、V/U 模式、网址识别等是未来诊断项，必须测试先行、逐项实现。不要增加 AI、云联想、语音或无关功能。

符号检索核心仍在，但中文分号已经恢复为标点，所以旧 `;sheshidu` TSF 入口不可再使用。后续只有在用户确认后，才设计可配置且不抢占常用标点的触发键。

## 开发纪律

- bug 和功能先写失败测试；
- 按键热路径不访问网络和磁盘；
- 候选快照不可异步跳变；
- 不破解商业输入法内部词库；
- 完成前必须执行干净构建、全部 CTest、真实外部词库和安装器集成验证；
- 每版更新版本说明、验证记录、项目上下文和续接文档。

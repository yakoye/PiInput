# PiInput v0.3.0-dev 验证记录

验证环境：Windows x64、Visual Studio 18 2026、MSVC 19.51、Windows SDK 10.0.26100.0。

## 构建

```text
配置：Release
架构：x64
所有 C++ 目标：构建成功
PiInputTSF.dll：构建成功
PiInput-Install.exe：构建成功
```

## 自动测试

```text
CTest：22/22 passed
失败：0
总测试时间：27.10 秒
```

通过的门禁包括：

- 核心输入、设置、候选网格、全拼变体、增量解码；
- 英文候选、事务词库转换、多进程学习合并；
- 安装布局、旧数据迁移和失败回滚；
- 品牌、SHA-256、发布元数据和 Windows 源码；
- 407 音节、786 条结构化语料；
- 性能冒烟、外部大词库增量性能；
- 两个真实 SCEL 回归。

## 安装布局

`cmake --install` 已确认生成：

```text
bin/PiInput-Install.exe
bin/PiInputTSF.dll
bin/piinput-profile.exe
bin/piinput-preview.exe
bin/piinput-cli.exe
bin/piinput-dictionary-builder.exe
bin/piinput-lexicon-compiler.exe
bin/piinput-scel-converter.exe
bin/piinput-benchmark.exe
data/base_lexicon.tsv
data/english_lexicon.tsv
data/sample_lexicon.tsv
data/symbols.tsv
```

## 用户包

```text
PiInput-v0.3.0-dev-windows-x64.zip
大小：2,319,923 字节
SHA-256：fdae317da7e2b70f245e768118945cf780813126ad602883a76524dc5095502c
```

压缩包已检查，最外层包含可直接双击的 `PiInput-Install.exe`、中文安装指南、`bin` 和 `data`。

## 旧版本卸载状态

旧 TSF Profile 和 COM DLL 已使用管理员权限注销。旧运行目录只剩被当前应用占用的 DLL，用户数据保留；PiInput 安装器会在迁移成功后将该 DLL 安排为重启删除。

## 人工测试边界

本轮按用户要求不自动安装 PiInput。最终的 `Win+Space` 可见性、记事本/浏览器/聊天程序输入体验、实际候选排序和 UI 观感由用户安装发布包后验证。

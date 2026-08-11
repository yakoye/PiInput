# PiInput

当前开发版本：`v0.3.0-dev`

PiInput 是一个轻量、快速、纯离线的中文输入法项目，优先完善全拼和小鹤双拼。项目不包含 AI、语音、广告、资讯或云端实时联想。

## Windows 用户

发布包完整解压后，双击最外层的：

```text
PiInput-Install.exe
```

安装器会完成版本并存安装、TSF 注册、旧用户数据迁移、冲突保留和锁定旧文件的重启清理。详细操作见 [安装与使用指南](docs/安装与使用指南.md)。

默认小鹤双拼示例：

```text
gjjt  → 感觉
jpiu  → 接触
cihv  → 词汇
mkt   → 明天（未完成编码候选）
rug   → 如果（未完成编码候选）
```

主要按键：

- 单独按 `Shift`：切换中文/英文；
- `Space`：上屏当前候选；
- `1`～`9`：选择当前行候选；
- `=` / `↓`：下一行；
- `-` / `↑`：上一行；
- `PageUp` / `PageDown`：翻页；
- `Enter`：上屏原始输入；
- `Esc`：取消。

## 当前能力

- C++20 跨平台输入核心；
- Windows TSF 原生输入法；
- 全拼与多双拼框架，小鹤双拼优先；
- `u/v` 兼容、隔音符、零声母和未完成音节候选；
- 任意长度增量解码与确定性候选排序；
- 横向多行候选窗，行列数可配置；
- 中文、英文和程序员标点模式；
- 141 项内置符号搜索；
- SCEL 转换、自有二进制词库和本地用户学习；
- 可选离线英文候选，默认关闭；
- 版本并存安装、旧数据迁移与失败回滚。

## 设置

设置文件位于：

```text
%LOCALAPPDATA%\PiInput\UserData\settings.ini
```

保存后在下一次开始输入时应用。完整字段与示例见 [安装与使用指南](docs/安装与使用指南.md)。

## 开发构建

需要 Visual Studio 2022/2026、MSVC x64、Windows SDK 和 CMake。仓库根目录运行：

```powershell
.\build.cmd -Clean
```

运行输入法专项测试：

```powershell
.\run-ime-tests.cmd
```

更新本地开源词库：

```powershell
.\update-dictionaries.cmd
```

大型词库固定放在仓库同级的 `dicts` 目录，不随源码更新删除。

生成 Windows 交付包：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\windows\package-release.ps1
```

## 主要产物

```text
PiInput-Install.exe
PiInputTSF.dll
piinput-profile.exe
piinput-preview.exe
piinput-cli.exe
piinput-dictionary-builder.exe
piinput-lexicon-compiler.exe
piinput-scel-converter.exe
piinput-benchmark.exe
```

## 测试

自动测试覆盖：

- 407 个标准全拼音节；
- 小鹤双拼键位、零声母、常用字词和非法编码；
- `接触`、`词汇`、`感觉`、`现在`、`中国`等常用词；
- 长句切分、专业词汇、歧义、纠错和候选稳定性；
- 所有内置符号与完整键盘标点映射；
- 候选网格、配置热加载和 Shift 状态机；
- 英文候选、事务词库更新和多进程学习合并；
- 安装布局、迁移、回滚、品牌和文件完整性；
- 真实 SCEL 与大型外部词库性能门禁。

用户提供的真实长句和 `786` 条结构化语料位于 `tests/corpus/v0.2.0`。

## 项目原则

1. 输入准确、顺序稳定和按键响应优先于功能数量；
2. 按键热路径不访问网络、不解析文本大词库、不同步写磁盘；
3. 专业词库不能无条件压过日常高频词；
4. 输入内容不变时，候选顺序不得异步跳动；
5. 用户词库、设置和学习数据只保存在本地，用户可控制；
6. 不加入 AI、语音、广告和无关内容。

## 文档

- [安装与使用指南](docs/安装与使用指南.md)
- [项目上下文](PROJECT_CONTEXT.md)
- [产品定义](docs/01_product_definition.md)
- [总体架构](docs/02_architecture.md)
- [开发任务](docs/03_development_tasks.md)
- [开发约束](docs/04_development_constraints.md)
- [词库与 SCEL](docs/06_dictionary_and_scel.md)
- [标点与符号](docs/07_symbols_and_punctuation.md)
- [测试与发布](docs/09_testing_and_release.md)
- [词库更新说明](docs/词库更新说明.md)
- [v0.3.0-dev 版本说明](docs/release_notes_v0.3.0-dev.md)
- [v0.3.0-dev 验证记录](docs/VERIFICATION_v0.3.0-dev.md)

# PiInput

当前发布版本：`v0.7.11`

PiInput 是一个轻量、快速、纯离线的中文输入法项目，优先完善全拼和小鹤双拼。项目不包含 AI、语音、广告、资讯或云端实时联想。

## Windows 用户

发布包完整解压后，双击最外层的：

```text
PiInput-Install.exe
```

安装器首次注册永久稳定的 TSF 入口，之后普通升级只切换独立 `PiInputHost.exe`，不再要求关闭 ChatGPT、Chrome、VS Code 等应用。它同时完成旧用户数据迁移、失败回滚和原生卸载注册。详细操作见 [安装与使用指南](docs/安装与使用指南.md) 与 [稳定入口与无重启升级说明](docs/稳定入口与无重启升级说明.md)。

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
- Windows TSF 永久轻量入口 + 可重启的独立 Host；
- 全拼与多双拼框架，小鹤双拼优先；
- `u/v` 兼容、隔音符、零声母和未完成音节候选；
- 全拼超级简拼：声母简拼、首字母简拼与简拼全拼混合（`srf`/`sruf`/`shrfa` 都是「输入法」）；
- 任务栏输入指示器里的 中/英 与 PiInput 两个按钮，右键菜单切换方案、打开设置与符号工具；
- 候选可以鼠标左键点选、右键固定或删除；
- 设置窗口覆盖引擎读取的全部选项，分五页；
- 任意长度增量解码与确定性候选排序；
- 精确长词、成语和诗词优先于逐字拼接候选；
- 普通候选耗尽后可用 `=` / `↓` 进入可撤销的分段取字，最后统一上屏；
- 用户分段组成或选择的词只在 TSF 确认上屏成功后学习；一次进入第一行、二至三次逐步前移；
- 候选右键可固定首位、取消固定或删除该词，删除后后续候选立即左移补位；
- `query-dictionary.cmd` 可按中文词条或标准拼音查询当前离线词库；
- 横向候选窗默认是无标题的 40-DIP 紧凑单行；按 `=`/`↓` 或 `-`/`↑` 后才展开为配置的多行；固定宽度还有空间时会按原排序从后续候选继续填满第一行；
- 候选窗优先锚定当前应用的 TSF 文本插入光标；极少数应用不提供文本几何位置时才退回鼠标附近；
- 中文、英文和程序员标点模式；
- 141 项内置符号搜索；
- SCEL 转换、自有二进制词库和本地用户学习；
- 可选离线英文候选，内置 24,323 词频词库、原始输入首选、前缀偏好排序和有界近似补全，默认关闭；
- 版本并存 Host、原子升级/回滚、旧数据迁移、原生安装器和原生卸载器；
- JSON 诊断工具显示实际永久 Shim、Host、协议和 Profile 状态。

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
PiInput-Uninstall.exe
PiInputTSF.dll
PiInputHost.exe
piinput-diagnostics.exe
piinput-profile.exe
piinput-preview.exe
piinput-cli.exe
piinput-dictionary-builder.exe
piinput-lexicon-compiler.exe
piinput-scel-converter.exe
piinput-benchmark.exe
```

`PiInput-Test.exe` 是不加载 TSF DLL 的独立测试台。它同时提供中文候选、英文候选和可自由输入、粘贴、换行的多行测试文本区，日常验证不必打开记事本或占用系统输入法 DLL。

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
- [v0.4.5 安装、使用与测试](docs/v0.4.5安装、使用与测试.md)
- [v0.4.6 安装、使用与测试](docs/v0.4.6安装、使用与测试.md)
- [v0.5.2 安装、使用与测试](docs/v0.5.2安装、使用与测试.md)
- [v0.5.3 安装、使用与测试](docs/v0.5.3安装、使用与测试.md)
- [v0.5.4 安装、使用与测试](docs/v0.5.4安装、使用与测试.md)
- [v0.5.5 安装、使用与测试](docs/v0.5.5安装、使用与测试.md)
- [v0.5.6 安装、使用与测试](docs/v0.5.6安装、使用与测试.md)
- [v0.5.8 安装、使用与测试](docs/v0.5.8安装、使用与测试.md)
- [v0.5.9 安装、使用与测试](docs/v0.5.9安装、使用与测试.md)
- [v0.7.2 安装、使用与测试](docs/v0.7.2安装、使用与测试.md)
- [v0.6.2 安装、使用与测试](docs/v0.6.2安装、使用与测试.md)
- [v0.6.1 安装、使用与测试](docs/v0.6.1安装、使用与测试.md)
- [v0.6.0 安装、使用与测试](docs/v0.6.0安装、使用与测试.md)
- [项目上下文](PROJECT_CONTEXT.md)
- [产品定义](docs/01_product_definition.md)
- [总体架构](docs/02_architecture.md)
- [开发任务](docs/03_development_tasks.md)
- [开发约束](docs/04_development_constraints.md)
- [词库与 SCEL](docs/06_dictionary_and_scel.md)
- [标点与符号](docs/07_symbols_and_punctuation.md)
- [测试与发布](docs/09_testing_and_release.md)
- [词库更新说明](docs/词库更新说明.md)
- [词库查询与分段取字](docs/词库查询与分段取字.md)
- [稳定入口与无重启升级说明](docs/稳定入口与无重启升级说明.md)
- [v0.3.3-dev 版本说明](docs/release_notes_v0.3.3-dev.md)
- [v0.3.3-dev 验证记录](docs/VERIFICATION_v0.3.3-dev.md)
- [v0.3.5-dev 版本说明](docs/release_notes_v0.3.5-dev.md)
- [v0.3.5-dev 验证记录](docs/VERIFICATION_v0.3.5-dev.md)
- [v0.4.1-dev 版本说明](docs/release_notes_v0.4.1-dev.md)
- [v0.4.1-dev 验证记录](docs/VERIFICATION_v0.4.1-dev.md)
- [v0.4.5-dev 版本说明](docs/release_notes_v0.4.5-dev.md)
- [v0.4.5-dev 验证记录](docs/VERIFICATION_v0.4.5-dev.md)
- [v0.4.6-dev 版本说明](docs/release_notes_v0.4.6-dev.md)
- [v0.4.6-dev 验证记录](docs/VERIFICATION_v0.4.6-dev.md)
- [v0.5.2-dev 版本说明](docs/release_notes_v0.5.2-dev.md)
- [v0.5.2-dev 验证记录](docs/VERIFICATION_v0.5.2-dev.md)
- [v0.5.3-dev 版本说明](docs/release_notes_v0.5.3-dev.md)
- [v0.5.3-dev 验证记录](docs/VERIFICATION_v0.5.3-dev.md)
- [v0.5.4-dev 版本说明](docs/release_notes_v0.5.4-dev.md)
- [v0.5.4-dev 验证记录](docs/VERIFICATION_v0.5.4-dev.md)
- [v0.5.5-dev 版本说明](docs/release_notes_v0.5.5-dev.md)
- [v0.5.5-dev 验证记录](docs/VERIFICATION_v0.5.5-dev.md)
- [v0.5.6-dev 版本说明](docs/release_notes_v0.5.6-dev.md)
- [v0.5.6-dev 验证记录](docs/VERIFICATION_v0.5.6-dev.md)
- [v0.5.8-dev 版本说明](docs/release_notes_v0.5.8-dev.md)
- [v0.5.8-dev 验证记录](docs/VERIFICATION_v0.5.8-dev.md)
- [v0.5.9-dev 版本说明](docs/release_notes_v0.5.9-dev.md)
- [v0.5.9-dev 验证记录](docs/VERIFICATION_v0.5.9-dev.md)
- [v0.7.11 版本说明](docs/release_notes_v0.7.11.md)
- [v0.7.10 版本说明](docs/release_notes_v0.7.10.md)
- [v0.7.9 版本说明](docs/release_notes_v0.7.9.md)
- [v0.7.8 版本说明](docs/release_notes_v0.7.8.md)
- [v0.7.7 版本说明](docs/release_notes_v0.7.7.md)
- [v0.7.6 版本说明](docs/release_notes_v0.7.6.md)
- [v0.7.5 版本说明](docs/release_notes_v0.7.5.md)
- [v0.7.4 版本说明](docs/release_notes_v0.7.4.md)
- [v0.7.3 版本说明](docs/release_notes_v0.7.3.md)
- [v0.7.2 版本说明](docs/release_notes_v0.7.2.md)
- [v0.7.2 验证记录](docs/VERIFICATION_v0.7.2.md)
- [v0.6.2-dev 版本说明](docs/release_notes_v0.6.2-dev.md)
- [v0.6.2-dev 验证记录](docs/VERIFICATION_v0.6.2-dev.md)
- [v0.6.1-dev 版本说明](docs/release_notes_v0.6.1-dev.md)
- [v0.6.1-dev 验证记录](docs/VERIFICATION_v0.6.1-dev.md)
- [v0.6.0-dev 版本说明](docs/release_notes_v0.6.0-dev.md)
- [v0.6.0-dev 验证记录](docs/VERIFICATION_v0.6.0-dev.md)
- [搜狗与 PiInput 候选对照](docs/搜狗与PiInput候选对照_2026-08-15.md)

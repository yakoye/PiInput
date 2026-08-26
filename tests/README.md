# tests 测试目录说明

## 目录分类

| 文件夹 | 内容 | 主要输入 |
|---|---|---|
| `unit` | C++ 单元和组件测试：引擎、候选、设置、用户模型、Host 协议、IPC、TSF 组件、安装布局 | `data`、仓库 `data`、临时目录 |
| `fixtures` | 可执行测试辅助程序：Host 客户端夹具、Controlled TSF Host/Controller 和共享协议 | 测试进程参数、临时 pipe、注册的 TSF 路径 |
| `regression` | PowerShell/CMake 集成回归：构建元数据、安装卸载、设置窗口、词库脚本、soak、源码结构 | `data`、`corpus`、构建出的 EXE/DLL、发布脚本 |
| `tools` | 测试专用运行器，目前是结构化语料候选运行器 | `corpus/v0.2.0` 与构建词库 |
| `data` | 小型、确定性的 TSV/JSON/文本夹具 | 被 unit 和 regression 读取 |
| `corpus` | v0.2.0 结构化输入法语料、方案、生成器和校验器 | 313 个可执行语料用例及生成结果模板 |

## 功能覆盖

- 输入引擎：`unit/test_main.cpp`、`incremental_decoder_tests.cpp`、`full_pinyin_variants_tests.cpp`、`segment_selection_tests.cpp`。
- 候选与标点：`candidate_grid_tests.cpp`、`key_event_gate_tests.cpp`、`paragraph_input_tests.cpp`；数据位于 `data/*candidates.tsv`、`punctuation_cases.tsv`。
- 用户词与设置：`user_model*`、`settings*`、`user_phrase_performance_tests.cpp`。
- Host/IPC：`host_*`、`pipe_*`、`session_manager_tests.cpp`，配合 `fixtures/host_client_fixture.cpp`。
- Windows TSF/UI：`candidate_presenter_tests.cpp`、`candidate_ui_element_tests.cpp`、`composition_mirror_tests.cpp`、`input_scope_policy_tests.cpp`。
- 安装发布：`installer_layout_tests.cpp`、`uninstall_layout_tests.cpp`、`migration_tests.cpp`，以及 `regression/*installer*`、`release_metadata_regression.cmake`、`windows_source_regression.cmake`。
- 一键更新：`regression/one_click_update_regression.ps1` 动态生成测试 ZIP，要求合法包 DryRun 通过、篡改包被 SHA-256 门禁拒绝；不会执行真实卸载或安装。
- 稳定性：`regression/host_soak_tests.ps1` 和 `tsf_app_soak_tests.ps1`；短 smoke 不能替代正式 8 小时门禁。
- 词库语料：`dictionary_builder_tests.cpp`、`lexicon_query_tests.cpp`、`regression/dictionary_*`、`structured_corpus_regression.ps1`。

## 运行方法

```powershell
# 日常完整构建和已注册自动测试
pwsh ./build.ps1 -Configuration Release

# 干净全量回归
pwsh ./build.ps1 -Configuration Release -Clean

# 已构建目录中单独运行 CTest
ctest --test-dir ./build/windows-x64 -C Release --output-on-failure
```

Controlled TSF、TSF/App 8 小时和 P0 真实宿主需要交互桌面或真实应用，必须与普通 CTest、Host-only soak 分开报告。

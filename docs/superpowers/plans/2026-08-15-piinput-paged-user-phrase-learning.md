# PiInput 候选分页、用户造词与丝滑输入实施计划

> **执行要求：** 严格按测试驱动方式逐项完成。每个行为先写失败测试、确认失败原因正确，再写最小实现。不得通过向生产词库添加“丝滑”“邓振铎”“叶鹏玉”等验收词通过测试。

**目标：** 让 `=`/`-` 成为稳定翻行键，让任意合法拼音通过真实词段和单字完成输入，并在 TSF 成功上屏后形成可跨应用复用的本地用户词；一次学习进入第一行，二至三次逐步前移，删除后候选左移。

**总体结构：** 保留只读静态词库和稳定 Host 架构，在 `UserModel` 中增加按规范拼音索引的可变用户词叠加层。`Engine` 把用户词作为真实候选来源参与确定性混排，`ImeSession` 负责分段暂存，`HostSession` 负责候选视图和待确认学习，TSF shim 只有在编辑会话成功后才向 Host 发送提交确认。用户模型由 Host 后台批量、原子持久化，按键热路径不写磁盘。

**技术栈：** C++20、CMake、CTest、Windows TSF/COM、现有 Host 命名管道协议、MSVC Release 构建。

**设计依据：** `docs/superpowers/specs/2026-08-15-piinput-paged-user-phrase-learning-design.md`

---

## Task 1：锁定现有缺陷并建立通用测试夹具

**修改文件：**

- `tests/candidate_grid_tests.cpp`
- `tests/host_session_tests.cpp`
- `tests/test_main.cpp`
- `tests/data/user_phrase_learning_lexicon.tsv`（新增）
- `CMakeLists.txt`

### 1.1 建立只含单字/词段、不含完整目标词的临时词库

测试数据只包含足以组成目标文字的单字或短词，并明确不包含完整目标词。夹具至少覆盖：

- 两音节普通词；
- 三音节姓名；
- 全拼和小鹤对应同一规范拼音；
- 同一拼音下多个基础候选；
- 一个需要翻到第二、第三行才能找到单字的组合；
- 一个多音字在不同规范拼音下分别存在的组合。

测试代码运行前断言完整目标词不在夹具中，防止测试因误加入整词而失真。

### 1.2 写候选翻行失败测试

新增断言：

- 当前列不为 0 时，`move_row(1)` 后列必须回到 0；
- `move_row(-1)` 后列必须回到 0；
- 5、6、7、8、9 个每行均覆盖；
- 不完整最后一行仍选中该行第一个；
- 到首行/末行不循环；
- 翻行后数字 `1` 选择新行第一个；
- 单纯翻行不改变候选内容或顺序。

### 1.3 写用户词缺失的失败测试

对只含词段的夹具验证：

1. 第一次完整查询不存在整词；
2. 分段选择能够逐段完成；
3. 现有 `record_committed_selection` 后再次查询仍不能主动生成整词（当前预期失败点）；
4. 当前 `=` 从非零列进入下一行后仍保留旧列（当前预期失败点）。

### 1.4 运行 RED

```powershell
cmake --build build/windows-x64 --config Release --target piinput-candidate-grid-tests piinput-host-session-tests piinput-core-tests --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "piinput-(candidate-grid|host-session|core-tests)" --output-on-failure
```

确认失败只来自新增行为，不接受编译环境或测试数据路径错误。

### 1.5 提交节点

```text
test: characterize paged candidate learning gaps
```

---

## Task 2：让 CandidateGrid 成为纯视图分页器

**修改文件：**

- `include/piinput/candidate_grid.h`
- `src/candidate_grid.cpp`
- `tests/candidate_grid_tests.cpp`

### 2.1 修改翻行语义

`CandidateGrid::move_row(int)` 在活动行发生变化时必须：

- 将 `active_column_` 强制设为 `0`；
- 再调用 `clamp_view()`；
- 保持 `candidate_count_`、候选顺序和选择 ID 不变；
- 在首行/末行无可移动时不改变列和视图。

### 2.2 补充显式返回值

把翻行是否实际变化暴露为返回值，或提供等价的 `try_move_row`，避免 Host 通过比较多个派生字段猜测是否移动。`move_page` 复用同一规则。

### 2.3 验证 GREEN

```powershell
cmake --build build/windows-x64 --config Release --target piinput-candidate-grid-tests --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "^piinput-candidate-grid$" --output-on-failure
```

### 2.4 提交节点

```text
fix: reset candidate focus when paging rows
```

---

## Task 3：重构 UserModel 为可查询的用户词叠加层

**修改文件：**

- `include/piinput/user_model.h`
- `src/user_model.cpp`
- `tests/test_main.cpp`
- `tests/user_model_tests.cpp`（新增，若从 core tests 拆分更清晰）
- `CMakeLists.txt`

### 3.1 先写新的 UserModel RED 测试

覆盖以下 API 行为：

```cpp
record_selection(pinyin, word)
record_composed_phrase(pinyin, word)
query_exact(pinyin)
pin(pinyin, word)
unpin(pinyin, word)
suppress(pinyin, word)
remove_learning(pinyin, word)
```

必须断言：

- `query_exact` 只返回完全相同规范拼音下的用户词；
- 用户组成词带 `user_created=true`；
- 普通基础候选被选择后带次数但不冒充用户创建词；
- suppression tombstone 在 `count==0` 时仍存在；
- 不同读音互不影响；
- 次数在 `uint32_t` 上限饱和；
- 查询 10,000 条用户词时不扫描全部记录。

### 3.2 改为按规范拼音嵌套索引

使用：

```cpp
std::unordered_map<
    std::string,
    std::unordered_map<std::string, UserEntry>
> entries_by_pinyin_;
```

`UserEntry` 公开只读快照字段：

- `selection_count`
- `last_used`
- `pinned`
- `suppressed`
- `user_created`

`query_exact` 复杂度限定为 `O(1 + k)`，`k` 为该拼音下的用户词数量。

### 3.3 引入明确学习等级

增加纯函数：

```cpp
learning_tier(selection_count)
```

规则固定为：

- 0 次：tier 0；
- 1 次：tier 1；
- 2 次：tier 2；
- 3 次及以上：tier 3；
- 3 次以后只提供递减的小幅稳定加权；
- pinned 独立于 tier，永远优先。

不再用 `count * 50000` 作为唯一排序机制。

### 3.4 验证 GREEN

```powershell
cmake --build build/windows-x64 --config Release --target piinput-core-tests piinput-user-model-tests --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "piinput-(core-tests|user-model)" --output-on-failure
```

### 3.5 提交节点

```text
feat: index local user phrases by canonical pinyin
```

---

## Task 4：升级用户模型格式并保证原子持久化

**修改文件：**

- `include/piinput/user_model.h`
- `src/user_model.cpp`
- `tests/user_model_tests.cpp`
- `docs/安装与使用指南.md`

### 4.1 写兼容性和损坏隔离 RED 测试

覆盖：

- 旧 4、5、6 列全部能读取；
- 新 7 列 `user_created` 能往返；
- 缺失第 7 列默认为 false；
- 单行损坏只跳过并报告该行，其余有效记录保留；
- 保存失败保留旧文件；
- 临时文件名唯一；
- 成功保存后没有残留临时文件；
- suppression tombstone 可往返。

### 4.2 实现安全保存

新格式：

```text
pinyin<TAB>word<TAB>count<TAB>last_used<TAB>pinned<TAB>suppressed<TAB>user_created
```

写入顺序：

1. 创建同目录唯一临时文件；
2. 写完整内容；
3. flush 并关闭；
4. Windows 下使用可替换现有目标的原子文件操作；
5. 失败时删除临时文件并保留旧目标。

### 4.3 验证 GREEN

```powershell
cmake --build build/windows-x64 --config Release --target piinput-user-model-tests --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "^piinput-user-model$" --output-on-failure
```

### 4.4 提交节点

```text
fix: persist user phrase state atomically
```

---

## Task 5：把用户词接入 Engine 候选来源和稳定混排

**修改文件：**

- `include/piinput/candidate_evidence.h`
- `include/piinput/engine.h`
- `src/engine.cpp`
- `include/piinput/incremental_decoder.h`
- `src/incremental_decoder.cpp`
- `tests/incremental_decoder_tests.cpp`
- `tests/lexicon_query_tests.cpp`
- `tests/user_model_tests.cpp`

### 5.1 写 Engine RED 测试

使用不含完整词的临时词库验证：

- `record_composed_phrase` 后完整用户词能主动生成候选；
- 一次学习进入第一行但不强制第一；
- 两次比一次更靠前；
- 三次具备首选竞争资格；
- pinned 始终第一；
- 第一行最多保留两个普通用户词；
- 用户词不足时不留空位；
- suppression 在合并所有来源后仍能过滤；
- 同一文字/拼音跨用户和静态来源去重；
- 同分时稳定 ID、文字和拼音打破平局；
- 同一 generation 重复查询顺序完全一致。

### 5.2 增加候选证据类型

为用户词增加明确证据：

```cpp
CandidateKind::user_phrase
```

候选保留 `learning_tier`、`selection_count`、`pinned` 和 `user_created` 排序信息，不把这些状态压缩成不可解释的超大整数。

### 5.3 合并算法

对每个完整规范拼音：

1. 从 `UserModel::query_exact` 取用户候选；
2. 从静态词库/解码器取基础候选；
3. 先按文字+规范拼音去重；
4. 应用 suppression；
5. pinned 放首位；
6. 选出最多两个 tier 1～3 用户词作为第一行保留；
7. 剩余位置由真实静态候选和短词填充；
8. 动态拼接只补空位；
9. 第一行之后继续使用同一稳定比较键。

第一行大小必须来自 `settings.candidates.items_per_row`，不能写死 6。

### 5.4 全拼/小鹤规范键共享测试

用同一 `Engine`：

- 全拼组成后小鹤查询可见；
- 小鹤组成后全拼查询可见；
- `u/v` 兼容只影响解析入口，不产生重复用户词。

### 5.5 验证 GREEN

```powershell
cmake --build build/windows-x64 --config Release --target piinput-incremental-decoder-tests piinput-lexicon-query-tests piinput-user-model-tests --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "piinput-(incremental-decoder|lexicon-query|user-model)" --output-on-failure
```

### 5.6 提交节点

```text
feat: merge learned phrases into stable candidate ranking
```

---

## Task 6：规范正常候选页与分段候选页的衔接

**修改文件：**

- `include/piinput/session.h`
- `src/session.cpp`
- `include/piinput/host_session.h`
- `src/host_session.cpp`
- `tests/segment_selection_tests.cpp`
- `tests/host_session_tests.cpp`

### 6.1 写分页状态机 RED 测试

覆盖：

- 正常候选的所有真实整词/完整拼音词先逐行展示；
- `=` 每次只进入下一行并选中第一个；
- 普通候选页耗尽后再按一次 `=` 才进入分段页；
- 进入分段页时第一个真实词段被选中，不跳到内部某个单字索引；
- 分段页排序为长真实词、短真实词、单字；
- 分段页 `-` 返回缓存的正常候选最后一行并选中第一个；
- 分段页内部 `=` 不重新生成候选；
- 正常候选快照恢复后顺序与进入前一致；
- 进入/退出模式后旧 candidate ID 失效；
- 首尾继续按 `=`/`-` 被吞掉，不输入 ASCII 符号。

### 6.2 用明确字段替换 trusted 前缀猜测

把 `trusted_candidate_count` 重命名/替换为含义明确的 `normal_browse_candidate_count`，它只统计：

- 完整用户词；
- 静态整词；
- 与完整输入相关的真实词；
- 单词级合法补全。

无意义动态拼句不进入普通翻页主区域。

### 6.3 缓存两个候选快照

`ImeSession` 保留：

- 正常候选快照；
- 当前未解决位置的分段候选快照。

同一模式内部翻行只移动 `CandidateGrid`。只有首次进入分段模式才按当前未解决位置生成一次分段快照；返回时恢复正常快照。

### 6.4 验证 GREEN

```powershell
cmake --build build/windows-x64 --config Release --target piinput-segment-selection-tests piinput-host-session-tests --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "piinput-(segment-selection|host-session)" --output-on-failure
```

### 6.5 提交节点

```text
fix: make word pages precede segment fallback
```

---

## Task 7：分段选择只在完整统一上屏后形成用户词

**修改文件：**

- `include/piinput/session.h`
- `src/session.cpp`
- `include/piinput/host_session.h`
- `src/host_session.cpp`
- `tests/segment_selection_tests.cpp`
- `tests/host_session_tests.cpp`

### 7.1 写分段学习 RED 测试

覆盖：

- 每个词段只消费声明的音节数；
- 已选文字保留在组合区；
- Backspace 撤销最后一段并恢复其拼音；
- 所有音节完成前不产生学习记录；
- 所有音节完成时只生成一次待提交结果；
- 两个及以上汉字才允许创建用户词；
- 未完成尾音、Esc、Enter 原始字母、动态假句不学习；
- 关闭用户学习时不创建记录。

### 7.2 区分普通选择与用户组成词

待学习数据包含：

```cpp
struct PendingLearning {
    std::string canonical_pinyin;
    std::string word;
    bool user_created;
};
```

- 正常候选明确选择：`user_created=false`；
- 分段完成：`user_created=true`；
- 原始字母提交：无待学习数据。

此任务只产生“待确认学习”，不得直接修改 `UserModel`。

### 7.3 验证 GREEN

```powershell
cmake --build build/windows-x64 --config Release --target piinput-segment-selection-tests piinput-host-session-tests --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "piinput-(segment-selection|host-session)" --output-on-failure
```

### 7.4 提交节点

```text
feat: stage composed phrases for confirmed learning
```

---

## Task 8：增加 TSF 成功上屏确认，杜绝误学

**修改文件：**

- `include/piinput/host_protocol.h`
- `src/host_protocol.cpp`
- `include/piinput/host_messages.h`
- `src/host_messages.cpp`
- `include/piinput/host_session.h`
- `src/host_session.cpp`
- `platform/windows/host/session_manager.h`
- `platform/windows/host/session_manager.cpp`
- `platform/windows/host/pipe_server.cpp`
- `platform/windows/tsf/pipe_client.h`
- `platform/windows/tsf/pipe_client.cpp`
- `platform/windows/tsf/composition_mirror.h`
- `platform/windows/tsf/composition_mirror.cpp`
- `platform/windows/tsf/stable_text_service.cpp`
- `tests/host_protocol_tests.cpp`
- `tests/host_messages_tests.cpp`
- `tests/host_session_tests.cpp`
- `tests/session_manager_tests.cpp`
- `tests/composition_mirror_tests.cpp`
- `tests/pipe_client_tests.cpp`

### 8.1 写协议和失败回滚 RED 测试

新增 `HostMessageType::commit_result` 和严格载荷：

```cpp
struct HostCommitResult {
    std::uint64_t generation;
    bool succeeded;
};
```

测试：

- 编码/解码成功；
- 截断、未知 flag、尾随字节拒绝；
- v1/v2 老消息继续解析；
- commit result 只能确认同 client/session/generation；
- 重复确认幂等；
- 过期确认不能学习；
- TSF 失败确认丢弃学习；
- TSF 成功确认才调用 `record_selection` 或 `record_composed_phrase`；
- 焦点切换/Host 恢复不能把失败提交误学。

### 8.2 Host 保存有限待确认记录

每个 `HostSession` 保存按 generation 索引、容量有限的待确认学习记录。创建 commit reply 时登记；收到结果时：

- success：写入用户模型并标记 dirty；
- failure：只删除待确认记录；
- unknown/expired generation：拒绝且不修改模型。

### 8.3 shim 在编辑会话完成后发送结果

`StableTextService` 必须以 `RequestEditSession` 和 `DoEditSession` 的实际结果为准：

- 两者都成功才发送 `succeeded=true`；
- 任一失败发送 false；
- 不以“Host 返回 commit”代替实际上屏成功；
- 结果发送失败不得阻止应用继续输入，但记录诊断信息。

### 8.4 验证 GREEN

```powershell
cmake --build build/windows-x64 --config Release --target piinput-host-protocol-tests piinput-host-messages-tests piinput-host-session-tests piinput-session-manager-tests piinput-composition-mirror-tests piinput-pipe-client-tests PiInputTSF PiInputHost --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "piinput-(host-protocol|host-messages|host-session|session-manager|composition-mirror|pipe-client)" --output-on-failure
```

### 8.5 提交节点

```text
fix: learn candidates only after confirmed TSF commit
```

---

## Task 9：实现 Host 后台批量保存与多窗口即时共享

**修改文件：**

- `platform/windows/host/user_model_persistence.h`（新增）
- `platform/windows/host/user_model_persistence.cpp`（新增）
- `platform/windows/host/host_runtime.h`
- `platform/windows/host/host_runtime.cpp`
- `platform/windows/host/main.cpp`
- `platform/windows/host/pipe_server.h`
- `platform/windows/host/pipe_server.cpp`
- `tests/user_model_persistence_tests.cpp`（新增）
- `tests/host_runtime_tests.cpp`
- `tests/host_process_tests.ps1`
- `CMakeLists.txt`

### 9.1 写无热路径磁盘写入 RED 测试

注入可计数存储后验证：

- 成功学习只更新共享 Engine 内存并置 dirty；
- 当前按键处理完成前写盘次数为 0；
- 1～2 秒空闲后只批量保存一次；
- 连续多次学习合并为一次保存；
- 保存期间新增学习不会丢失；
- Host 正常 drain/退出前强制 flush；
- 保存失败保持 dirty 并可重试；
- 另一个 Session 的新 generation 立即看到已学习词；
- 已显示的旧 generation 不后台跳变。

### 9.2 实现持久化协调器

`UserModelPersistence` 只负责：

- dirty revision；
- 最后修改时间；
- 到期保存调度；
- drain/退出强制保存；
- 错误记录和重试。

不复制 Engine，不重载静态词库，不在 TSF DLL 中持有用户模型。

### 9.3 Host 事件循环接入

PipeServer 以有界超时唤醒检查到期保存，不能引入忙轮询。候选右键固定/删除同样只置 dirty，不再在回调中同步 `save_user_model`。

### 9.4 验证 GREEN

```powershell
cmake --build build/windows-x64 --config Release --target piinput-user-model-persistence-tests piinput-host-runtime-tests PiInputHost --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "piinput-(user-model-persistence|host-runtime|host-process)" --output-on-failure
```

### 9.5 提交节点

```text
perf: batch user model persistence in host
```

---

## Task 10：完成固定、删除、取消固定与候选左移

**修改文件：**

- `include/piinput/host_session.h`
- `src/host_session.cpp`
- `include/piinput/session.h`
- `src/session.cpp`
- `platform/windows/host/session_manager.h`
- `platform/windows/host/session_manager.cpp`
- `platform/windows/host/candidate_presenter.cpp`
- `platform/windows/tsf/candidate_window.cpp`
- `tests/host_session_tests.cpp`
- `tests/candidate_presenter_tests.cpp`

### 10.1 扩展候选管理动作

明确动作：

```cpp
pin_first
unpin
delete_candidate
dismiss
```

删除逻辑：

- 用户创建词：清除学习，保留 suppression tombstone；
- 静态词：只写 suppression tombstone；
- 只影响相同规范拼音+文字；
- 删除后新 generation；
- 候选立即重算，后续候选左移；
- 高亮尽量保持原屏幕位置，越界时夹到末项；
- 旧 ID 无效；
- 取消固定不删除学习记录。

### 10.2 写 UI/Host RED 测试

覆盖右键菜单动作映射、删除后左移、末项删除、不同读音、重启持久化、取消固定和 Esc/点击外部区域取消组合。

### 10.3 验证 GREEN

```powershell
cmake --build build/windows-x64 --config Release --target piinput-host-session-tests piinput-candidate-presenter-tests PiInputHost --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "piinput-(host-session|candidate-presenter)" --output-on-failure
```

### 10.4 提交节点

```text
feat: manage learned candidates with stable left shift
```

---

## Task 11：增加真实性能门禁和非硬编码回归

**修改文件：**

- `tools/benchmark/main.cpp`
- `tests/user_phrase_performance_tests.cpp`（新增）
- `tests/paragraph_input_tests.cpp`
- `tests/data/unseen_generalization_corpus_2026-08-15.txt`
- `tests/brand_filter_regression.cmake`
- `tests/release_metadata_regression.cmake`
- `CMakeLists.txt`

### 11.1 性能数据规模

使用：

- 至少 100,000 条静态词；
- 10,000 条用户词；
- 多个用户词共享拼音；
- 冷查询、热查询、翻行、学习、后台保存分别测量。

### 11.2 门槛

```text
用户词精确查询 P95 <= 0.2 ms
完整核心候选查询 P95 <= 2 ms
候选翻行 P95 <= 0.1 ms
内存学习更新 P95 <= 0.2 ms
按键热路径磁盘写入 = 0
```

性能失败必须显示样本数、P50/P95/P99 和加载词条数。

### 11.3 非硬编码门禁

发布数据、生产源码和常量中不得出现测试目标整词。测试夹具可以出现，但必须位于 `tests/`。新增通用未见词测试，每次从夹具生成不同文字组合，证明算法不是针对单词补丁。

### 11.4 验证 GREEN

```powershell
cmake --build build/windows-x64 --config Release --target piinput-user-phrase-performance-tests piinput-benchmark piinput-paragraph-input-tests --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "piinput-(user-phrase-performance|external-incremental-performance|paragraph-input|brand-filter)" --output-on-failure
```

### 11.5 提交节点

```text
test: gate user phrase latency and generalization
```

---

## Task 12：更新文档、发布元数据并完成全量验证

**修改文件：**

- `README.md`
- `PROJECT_CONTEXT.md`
- `docs/安装与使用指南.md`
- `docs/词库查询与分段取字.md`
- `docs/09_testing_and_release.md`
- `docs/VERIFICATION_vNEXT-dev.md`（按实际版本命名）
- `docs/release_notes_vNEXT-dev.md`（按实际版本命名）
- `VERSION`
- `RELEASE_MANIFEST.md`
- `FILE_LIST.txt`
- `SHA256SUMS.txt`

### 12.1 用户文档必须说明

- `=`/下键下一行，`-`/上键上一行；
- 每次翻行选中第一个；
- 如何用词段/单字组成缺失词；
- 一次、两次、三次学习的可见效果；
- 如何固定首位、取消固定、删除误学词；
- 删除后为什么其他候选左移；
- 全拼和小鹤共享用户词；
- 用户模型路径、备份和恢复；
- 禁用用户学习后的行为；
- 安装、卸载、升级和便携测试步骤。

### 12.2 完整 Release 构建

```powershell
.\build.ps1 -Configuration Release -Clean
```

必须确认所有目标编译成功，不能只看某个测试目标。

### 12.3 完整 CTest

```powershell
ctest --test-dir build/windows-x64 -C Release --output-on-failure --parallel 1
```

要求 0 failed；外部大词库和 SCEL 门禁若因数据缺失跳过，必须在验证文档中明确，不能写成通过。

### 12.4 安装布局与产物检查

```powershell
cmake --install build/windows-x64 --config Release --prefix dist/windows-x64
```

确认 Host、TSF shim、安装器、卸载器、设置程序、测试程序、词库和文档齐全。

### 12.5 Windows 手工验收

在不清理用户现有数据的测试配置中完成：

1. Notepad++：组成一个静态词库不存在的新词，再次输入进入第一行；
2. Notepad4：连续 `=`/`-` 验证每行首项和无循环；
3. PiInput-Test：失败提交不学习，成功提交才学习；
4. Chrome/Word/Codex：新 generation 跨应用立即可见；
5. 删除后候选左移，重新输入仍不出现；
6. 一次、两次、三次选择逐步前移；
7. 固定/取消固定行为正确；
8. 候选未变化时窗口不跳动；
9. 首次输入和切换应用无秒级停顿。

记录实际耗时、首选位置、翻行次数和异常，不允许只写“肉眼正常”。

### 12.6 刷新发布校验

按仓库现有白名单机制刷新 `FILE_LIST.txt` 和 `SHA256SUMS.txt`，确认不包含 `build/`、`dist/`、外部 `dicts/` 或用户数据。

### 12.7 最终检查

```powershell
git diff --check
git status --short
```

只提交本轮明确修改；现有工作区中与本轮无关的用户改动保持原样。

### 12.8 提交节点

```text
docs: document paged candidate learning workflow
```

---

## 完成判定

本计划只有在以下证据同时存在时才完成：

1. 每个新增行为都有 RED 和 GREEN 记录；
2. 新用户词由算法生成，不存在生产硬编码；
3. `=`/`-` 每行首项规则在 5～9 列全部通过；
4. 用户组成词一次进入第一行、二至三次逐步前移；
5. TSF 失败提交绝不学习；
6. 删除后候选左移且重启仍被屏蔽；
7. 多应用共享新学习，旧候选 generation 不跳变；
8. 热路径无磁盘写入且达到性能门槛；
9. Release 全目标和全量 CTest 通过；
10. 安装包、便携包和 Markdown 使用说明齐全。

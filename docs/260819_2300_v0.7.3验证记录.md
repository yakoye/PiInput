# PiInput v0.7.3 验证记录

## 修复依据

- **逐键候选冻结**：小鹤 `xpzdw`→`xpzdwj`→`xpzdwji` 三次按键候选完全不变。`Engine::compose_full_coverage_unlocked` 入口是 `count < min_span_syllables * 2`，三音节 `xie'zai'wan` 从未进入拼接；每段又要求至少 2 音节且拒绝单字条目，「卸载」+「完」这个形状永远拼不出来。
- **未完成音节被忽略**：尾部半音节只走 `query_completions_unlocked` 的整词补全，上限是 `complete + 1` 个音节。「卸载完」不是词条，「卸载完了」是 4 音节被上限挡掉，于是 `xpzdw` 的 `w` 等同没打。
- **全拼中间态塌回**：`Engine::query` 只用 `primary_parse`，而它优先取 `trailing_prefix` 为空的读法。只要输入能完整切分，`expand_input_prefix` 算出的前缀读法就被整体丢弃。全拼里 `chen` 是合法音节同时也是 `cheng` 的开头，所以 `xiezaiwanchen` 退回「卸载 写在」，`xieza` 退回单字，`jintia` 给出「浸提啊」。
- **拼接压过真实词条**：补全出的词库整词是 group 1，拼接是 group 0，`woxianza` 的「我先咋」「我先砸」把词库里的「我现在」压到第 8。

## 修复方式

- 拼接接受 `trailing_prefix`，把「已确定的词 + 正在输入的音节」并入同一套有界动态规划。单字可以收尾，但必须由多字实词打底、至多一个、且只能落在输入末尾；无实词打底的字串联仍然拒绝。
- 未完成音节的补全池先查一次 `canonical_prefix` 的长词前缀，取出该音节的真实读法作为上下文证据优先，否则纯词频排序会让「我」压过「完」。
- 拼接与补全提取为 `emit_joins_and_completions`。最后一个音节还能生长时（`syllable_can_grow` 预计算 413 个标准音节的真前缀集合），把词尾按音节逐级回退再调用一次，回退 1 步覆盖 `chen`→`cheng`，回退 2 步覆盖已切成 `ti'a` 的 `tian`。回退后剩余音节必须非空。
- 音节回退只对全拼开启。双拼一个音节固定两键，读法不随下一次按键生长，`小鹤` 回退成 `he` 再补全会给出键盘拼不出的「小黑」。
- 补全出的词库整词改为 group 0，与拼接同级，由「词数少者优先」决出先后。
- 词库存在整词覆盖全部音节时，拼接完全关闭，避免「我爱尼」挤掉「我爱」。

## 契约变更

v0.7.1 记录的「不机械拼句」放开了一半：实词加一个收尾单字现在允许，纯单字串联仍然禁止。

- `tests/host_session_tests.cpp` 原断言「非常快」绝不出现，改为断言它排在首位，并新增一条断言守住 `kadun` 不出现「卡吨/卡炖/卡蹲」——v0.6.2 修过的问题不会回来，那是两个单字、没有实词打底。

## 自动验证

- Windows x64 Release 全目标构建：通过；
- 完整 CTest：59/59 通过，0 失败，总耗时 84.32 秒；
- `piinput-core-tests`：通过，新增 16 条逐键回归（`tests/data/incremental_join_cases.tsv`），写测试时 11 条红、5 条绿；
- `piinput-host-session`：通过，拼接契约断言已更新，新增单字串联防御；
- `piinput-host-process`：通过。此前一版因音节回退未限定 schema，双拼 `xnhe` 首选变成「小黑」被这条测试拦下；
- `piinput-keystroke-latency`：通过，`key_p95_us` 172~194、`key_max_us` 232~246，均优于修复前的 253/343；
- 长输入单查询 p95：`woxianza` 36.7、`jintianwanshang` 42.0、`xiezaiwanchen` 50.7、`jintianwansha` 56.3 微秒，预算 2000 微秒；
- 发布版本、文件清单与源码 SHA-256 门禁：通过。

## 人工验证要点

小鹤双拼逐键：

```text
xpzdw    → 卸载完 卸载万 卸载晚 卸载 写在
xpzdwj   → 卸载完 卸载万 卸载 写在 些
xpzdwji  → 卸载完成 写在完成 卸载玩车 卸载
xpzdwjig → 卸载完成 写在完成 卸载 写在
```

全拼逐字母，整句全程不掉：

```text
jint → jinti → jintia → jintian → jintianwan → jintianwans → jintianwansha
今天   今天    今天     今天      今天完       今天晚上      今天晚上
```

回归确认无变化：`kadun`→卡顿、`woaini`→我爱你、`nihaoma`→你好吗、`jisrji`→计算机、`gjjt`→感觉、`mkt`→明天、`xnhe`→小河/小盒/小鹤。

## 已知未解

全拼 `xie` 首选是「西鄂」而不是「些」。`xi'e` 的整词命中 group 0，`xie` 的单字是 group 2，group 比 `parse_rank` 先比较，劣质切分因此赢下首位。既有行为，与本版改动无关。

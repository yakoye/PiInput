# PiInput v0.8.3 验证记录

验证日期：2026-09-01

<!-- release-gates
host_soak_8h=NOT_RUN
tsf_app_soak_8h=NOT_RUN
p0_real_host_matrix=NOT_RUN
installer_ui_real_run=NOT_RUN
english_completion_real_run=NOT_RUN
-->

## 自动验证

- 干净 Release 构建，全量 CTest 通过（68 项）。
- 新增单元测试：`IS_PRIVATE` 不再拒绝按键但仍被识别为「不该记忆」、密码与 PIN 五项仍拒绝转换、Shift 与未接管键组成的和弦不得读成单击、探询回调重复记录与记录一次等效。
- 新增源码门禁：必须注册 `GUID_TFCAT_TIPCAP_COMLESS`；不得注册 `GUID_TFCAT_TIPCAP_SECUREMODE`；`IS_PRIVATE` 不得出现在拒绝转换的集合里而四个密码/PIN 必须在；探询回调里只允许调用不改变状态的和弦记录方法，三个会改状态的入口仍禁止。

## 实机验证（本机，带追踪取证）

三条修复都是先取到数据再动代码，改完用同样方式复验。

**Chrome 标签重命名**——`key_refused` 埋点一次定位：

```
450992609  key_down vk5A(Z) → key_refused  sensitive_scope   ← 重命名框，13 个键全部如此
451004515  key_down vk5A(Z) → key_dispatch letter:z          ← 同进程的地址栏，正常
```

修复后由用户实机确认书签重命名可输入中文。

**yesymbol / MTA 加载**——专门构建的 MTA 进程，每次只激活一个配置以排除顺序因素：

| 输入法 | 修复前 | 修复后 |
| --- | --- | --- |
| 搜狗 `SogouTSF.ime` | 加载 | 加载 |
| 微信 `wetype_tip.dll` | 加载 | 加载 |
| 小狼毫 `weasel.dll` | 加载 | 加载 |
| **PiInput** | **不加载** | **加载** |

四家的 COM 线程模型均为 `Apartment`，`CoCreateInstance` 在 MTA 下四家全部成功——因此可排除 DLL 本身、注册路径与线程模型，差异只在分类声明。修复后由用户实机确认 yesymbol 可输入中文。

**回调模式差异**（本版观测到，影响 Shift 修复的落点）：

| 应用 | OnTestKeyDown | OnKeyDown |
| --- | --- | --- |
| chrome / ChatGPT / claude | 0 | 141 / 2104 / 978 |
| Weixin / explorer | 126 / 79 | 112 / 30 |

Chromium 系一次都不调用询问回调，而 MobaXterm 每个键都问、只交出被接管的。两个回调谁都看不全，这是把和弦观测放进询问回调的原因。

## 已确认的行为

- **`IS_PRIVATE` 不是密码域。** 它标记的是「不要记住这段文本」。Chrome 给普通文本框打这个标记，当成密码处理会让整个框无法输入。
- **TSF 在 MTA 线程上静默跳过没有声明 COM-less 的输入法。** 激活调用返回成功，DLL 不出现，没有任何错误码可查。只能靠对照实验发现。
- **`ImmDisableIME` 之后没有任何输入法能加载。** 实测：搜狗、微信输入法、小狼毫、PiInput、语音输入全部不加载。YeImageViewer 调用了它，那里的中文输入问题不在 PiInput 的可修范围内。

## 排查过程中被数据推翻的判断

记录在此，因为它们说明哪些推理方式在这个问题域里不可靠。

- **「所有窗口键盘布局一致，故每窗口独立输入法已排除」**——不成立。TSF 输入法共用同一个语言 HKL，`GetKeyboardLayout` 分辨不出是哪个输入法。
- **「PiInput 未加载，故任何输入法都进不去」**——不成立。当时激活的就是 PiInput，其他输入法本就不该在。
- **「TSF 在 MTA 下不承载任何文本服务」**——不成立。只测了 PiInput 就推广，实际搜狗、微信、小狼毫都能加载。

三次都是从单点观察推广出普遍结论。最后是逐个激活的对照实验定的案。

## 尚未验证（阻止转为正式版）

- Host 8 小时、TSF/App 8 小时长稳未运行。`tsf_app_soak` 的用例准备用 `SetWindowTextW` 写种子文本，TSF 看不见，soak 循环从未真正开始，详见 `docs/待办事项.md`。
- P0 真实宿主矩阵未运行。
- 安装器界面未实机运行。
- 中文模式英文候选未实机使用（默认关闭，本版未触及）。
- **无可信 Authenticode 签名与 RFC 3161 时间戳。** 与前版相同，对开启智能应用控制强制模式的机器是硬阻塞。

## 本版遗留的已知问题

- **`IS_PRIVATE` 字段现在能输入，但仍会被学习。** 正确做法是照常输入、不记入用户词库；shim 看得到输入域而 Host 负责学习，中间需要协议新增字段。已记入待办，未半做。
- MobaXterm 里候选框不跟随光标，原理上做不到，详见 `docs/待办事项.md`。

## 结论

三条修复均有实机前后对照数据支撑，其中两条由用户在真实程序中确认。自动回归完整通过。

长稳、宿主矩阵、安装器界面均未实机验证，签名门禁未完成。**只能作为候选版分发，不得转入 `releases/current`。**

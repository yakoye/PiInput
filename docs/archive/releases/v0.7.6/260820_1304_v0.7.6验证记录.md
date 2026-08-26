# PiInput v0.7.6 验证记录

## 来源

用户在 v0.7.5 上的四个实机反馈，记录在 `docs/待办问题清单.md`。四个全部处理完毕。

## #1 微信输入框中候选窗遮住文字

**取证**：`CandidatePresenter` 已有光标定位日志（`%TEMP%\piinput-caret-trace.on` 作开关，写 `piinput-caret-trace.csv`），字段含 `has_caret` 与 caret/bar 两个矩形。用户在微信复现后取新增的 65 条。

坐标系先要对齐：第一次用 PowerShell 查工作区得到 2048x1152，那是 DPI 虚拟化后的值；`SetProcessDPIAware` 之后是真实的 2560x1440，工作区 0..1380。微信窗口 `x=1636~2540`，据此才能从日志里认出哪几条是微信。

```text
微信      caret=(2201,1071,2203,1073)  高 2   bar=(1809,1078,2555,1128)
普通程序  caret=(710,1169,710,1193)    高 24  bar=(710,1198,1300,1248)
```

**根因**：微信报告的插入点矩形只有 2 像素高，远小于它实际的文字行。`place_candidate_window` 用 `caret.bottom + gap` 锚定，于是候选窗落在文字行的下半部分。普通程序报告 24 像素，同样的公式就正常。

**修复**：锚定前把插入点补足到至少一行文字高（`16 * dpi / 96`）。这是通用规则而非微信特例——Electron 系应用普遍报告过小的插入点矩形。报告正常行高的程序行为完全不变。

**既有测试的调整**：`candidate_presenter_tests.cpp` 中 200% DPI 那条用的是 24 像素高的 caret，在 200% 缩放下相当于半行，与新规则冲突。改为 48 像素——该 DPI 下一行文字的真实高度，测试原本要验证的「间隙随 DPI 缩放」意图不变。

## #2 候选窗鼠标点击卡死

**根因**：候选窗由 Host 主线程创建（`main.cpp` 里 `presenter.create()` 之后直接 `server.run()`），Windows 只向创建窗口的线程投递窗口消息。`PipeServer::run()` 同步阻塞在 `ConnectNamedPipe`，消息泵只在循环末尾、处理完一次请求后跑一次。空闲时窗口完全不处理消息：点击进队列不被派发，工具栏回调不触发，Windows 判定无响应。

**修复**：新增 `ConnectionWaiter`，把 `ConnectNamedPipe` 放到工作线程，主线程改用 `MsgWaitForMultipleObjects(1, &completed, FALSE, INFINITE, QS_ALLINPUT)` 同时等待连接完成与窗口消息。

**为什么不用 overlapped 管道**：句柄一旦带 `FILE_FLAG_OVERLAPPED`，该句柄上每一次 `ReadFile`/`WriteFile` 都要改写。按键热路径已为卡顿问题返工多轮（v0.5.8、v0.6.0、v0.6.1），把改动限制在「等待连接」这一步风险最低。

**测试缺陷**：`--toolbar-responsive` 早就存在，错误信息也写着 "while the pipe server was idle"，但它在按键请求返回后立即点击，恰好赶在 Host 那一轮消息泵之前，是个竞态，因此一直是绿的。现在点击前 `Sleep(600)`，加上等待后立刻复现失败。

## #3 候选词耗尽后没有单字兜底

**根因**：`mk`、`dddyn` 这类纯声母输入在常规路径下完全无解析——`m` 不是合法音节，`mk` 也不是任何音节的前缀，`expand_input_prefix` 返回空。整个 `Engine::query` 里只有简拼那条路产生候选，单字兜底从未被触发。对照：单音节 `m` 有 12 个单字候选，走的是未完成音节补全，机制一直在，只是简拼场景够不着。

**修复**：简拼词组之后，用第一个声母的单字继续填充（复用 `query_completions_unlocked`），分组为单字兜底级别，排在词组之后。

## #4 PiInput-Settings.exe 打不开

**根因**：`CreateMutexW` 只承诺「命名互斥体已存在时 `GetLastError()` 返回 `ERROR_ALREADY_EXISTS`」，不承诺新建成功时清零 last error。调用前没有 `SetLastError`，`InitCommonControlsEx` 残留的错误码可能被误判为「已有实例」，随即静默 `return 0`。

在用户机器上跑现有 exe 确认：进程立即退出、退出码 0，排除了 `return 1`（注册窗口类失败）与 `return 2`（创建窗口失败）。

**修复**：调用前 `SetLastError(ERROR_SUCCESS)`；判定为「已存在」但 `FindWindow` 找不到窗口时视为残留互斥体，继续创建自己的窗口。

## 自动验证

- Windows x64 Release 全目标构建：通过；
- 完整 CTest：60/60 通过；
- 新增回归：过矮插入点不遮挡文字（`candidate_presenter_tests.cpp`）、设置窗口必须出现（`settings_window_regression.ps1`）、空闲点击必须被处理（`host_client_fixture.cpp` 加等待）；
- `piinput-keystroke-latency`：`key_p95_us` 171~175、`key_max_us` 234~245，与 v0.7.5 一致；
- Host 端请求延迟门禁（6ms P95 / 12ms max）：通过。

## 人工验证要点

见 `v0.7.6安装、使用与测试.md`。四条：微信里候选窗不压文字、候选窗停一会儿再点要有反应、`mk` 要有单字、设置程序要能打开。

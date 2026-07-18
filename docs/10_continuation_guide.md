# 新会话续接说明

## 给下一位开发者或新会话的第一条指令

请先完整阅读：

1. `PROJECT_CONTEXT.md`；
2. `README.md`；
3. `docs/03_development_tasks.md`；
4. 最新 `docs/release_notes_v*.md`；
5. 最新 `docs/VERIFICATION_v*.md`；
6. 最新 `docs/next_develop_plan_v*.md`；
7. `docs/TSF_DEVELOPER_TEST.md`。

不要仅根据聊天摘要猜测项目状态。

## 当前基线

- 版本：`v0.1.5-dev`；
- 核心：C++20；
- Windows 工具链：Visual Studio 2026/2022 + CMake；
- 默认开发输入方案：小鹤双拼；
- 已有内置起步基础词库、SCEL 工具、二进制词库、输入核心、符号和学习模型；
- 已新增第一版 `LiteImeTSF.dll` 和 TSF 注册脚本；
- 用户第一轮 Windows 构建已成功编译核心和工具；v0.1.5 已修复 Profile Manager CLSID 与 `msctf.lib` 两个 TSF 阻断，等待二轮日志；
- 当前最重要任务是打通用户机器上的真实 Windows 系统输入链路。

## v0.1.3 无候选问题的结论

用户在独立预览中选择小鹤双拼并输入 `jisrji`，候选为空：

1. v0.1.3 预览只是查询窗口，不是系统输入法；
2. 当时只导入两个专业 SCEL；
3. 专业词库不保证包含普通词“计算机”；
4. 因此标准拼音解码正确，但词库没有精确候选。

v0.1.4 新增内置基础词库，并在安装时始终与专业 SCEL 合并。自动测试要求：

```text
full:  jisuanji -> 计算机
flypy: jisrji   -> 计算机
```

## 本地恢复与安装步骤

```text
1. 删除或改名旧 lite-ime-dev
2. 解压 lite-ime-v0.1.5-dev.zip
3. 得到新的 lite-ime-dev
4. 确保同级 dicts 中有用户 SCEL
5. 进入 lite-ime-dev
6. 运行 setup-dev.cmd
7. 记录从 Using CMake 开始的完整输出
8. 安装成功后重新打开记事本
9. Win+Space 选择 LiteIME 中文输入法（开发版）
10. 输入 jisrji 并按 Space
```

## 用户 Windows 验收时优先收集的信息

- CMake 配置和 MSVC 编译完整日志；
- 是否生成 `LiteImeTSF.dll` 和 `liteime-profile.exe`；
- `regsvr32` 是否成功；
- Win+Space 是否出现 LiteIME；
- 记事本中按键是否被捕获；
- 是否出现 Composition 文本；
- 是否出现候选窗；
- 空格是否上屏；
- 关闭记事本后是否崩溃或残留；
- `verify-windows.ps1` 完整输出。

## 开发纪律

- 先写失败测试，再实现；
- 不通过加延迟掩盖竞态和候选跳变；
- 每个候选选择必须绑定快照 generation 和候选 ID；
- 按键热路径不得访问网络；
- 不把 SCEL 作为运行时词库；
- Windows 平台代码不得进入跨平台核心；
- 构建、测试、安装任一失败必须停止脚本；
- 没有实际验证，不得写“已完成”；
- Windows 未实际构建时，只能写“源码已实现，待 Windows 验证”；
- 每版必须更新完整文档和下一版计划；
- 源码发布包不得包含 build、dist、`.vs` 和旧二进制。

## 用户固定偏好

- 中文沟通；
- 文件名尽量使用英文；
- 每次项目更新提供准确 Git 命令；
- 版本 ZIP 必须带版本号；
- 本地开发目录固定为 `lite-ime-dev`；
- 每版 ZIP 内携带全部上下文文档，方便换会话继续；
- 发布包必须可以从根目录运行构建、测试和安装脚本。

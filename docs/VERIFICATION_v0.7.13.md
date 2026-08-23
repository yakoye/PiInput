# PiInput v0.7.13 验证记录

验证日期：2026-08-23

<!-- release-gates
host_soak_8h=PASS
tsf_app_soak_8h=NOT_RUN
p0_real_host_matrix=NOT_RUN
-->

## 自动验证

- 专业词结构化回归：59/59；结构化语料总计 313 个用例。
- 大词库：928,725 条；`.lex` 使用只读内存映射，映射字节数由 Host 与 benchmark 对外报告。
- 映射后本机 Host 进程测试：冷启动健康检查 576 ms，首次请求 25 ms，常驻请求 15 ms。
- 前候选 Host-only 8 小时证据：`artifacts/soak-8h-20260823/summary.json` 为 `passed`，运行 2026-08-23 06:12:48Z–14:12:53Z，共 958 个样本；build ID 为 `0.7.13+e3e6c2dc1784-dirty`，词库以 `mmap` 映射 40,758,365 bytes。Private Bytes 从 10,223,616 降至 9,506,816（-716,800；斜率 +35,308.91 bytes/h），Working Set 从 57,978,880 降至 51,445,760（-6,533,120），Handle 从 124 降至 123（-1；斜率 -0.0070/h），全部在阈值内。该运行启动后源码继续变化，只能证明前候选稳定性，不能填写机器 Gate `host_soak_8h=PASS`。
- 最终候选 Host-only 8 小时 Gate：冻结提交 `a2d5f8fe3c53`，build ID 精确为 `0.7.13+a2d5f8fe3c53`；`artifacts/soak-8h-final-20260823-a2d5f8fe3c53/summary.json` 为 `passed`，运行 2026-08-23 14:24:32Z–22:24:37Z，共 957 个样本，`mmap` 40,758,365 bytes。Private Bytes 10,166,272 → 9,547,776（-618,496；斜率 +7,432.79 bytes/h），Working Set 57,970,688 → 57,675,776（-294,912），Handle 124 → 123（-1；斜率 -0.0070/h）；峰值分别为 10,498,048 bytes、58,466,304 bytes、125，全部低于门槛。因此 `host_soak_8h=PASS`。
- Windows 包闭环静态正向与哈希/build ID 失败路径已实跑；脚本覆盖压缩包哈希、精确 commit build ID、RFC3161 签名证据、前版 ZIP 覆盖升级与 UserData 哨兵、当前版安装×2、安装后 Host/TSF 哈希与注册路径、Controlled TSF 实际 DLL、静默卸载和可选重装。正式签名/提权路径尚未执行。
- TSF/App soak harness 的 fixture 正向、低工作负载密度失败、context 重建计数和双进程资源 summary 已验证；它不加载 PiInput，不能填写 `tsf_app_soak_8h=PASS`。
- tag workflow 已 fail-closed：外部 Gate 未全 PASS 不进入构建/签名；PFX secret 仅存在于单一签名步骤，失败路径保留 JUnit 与阶段化 JSON。
- 统一结果聚合器的正向、非法状态、重复 Case ID 和缺失 artifact 路径已通过；CI `always()` 收尾会输出 schema v1 `result.json` 与逐文件 SHA-256 manifest，但在线 workflow 证据尚未产生。

## 尚需正式环境完成

- 本机当前没有可用代码签名证书；未签名候选只能用于开发验证。正式标签流水线必须配置 `PIINPUT_SIGNING_PFX_BASE64` 与 `PIINPUT_SIGNING_PFX_PASSWORD`。
- 冻结候选 `a2d5f8fe3c53` 的 Host-only 8 小时已完成并通过；后续若修改实现或测试代码，必须形成新候选并重新执行，证据文档更新不改变已验证二进制身份。
- 当前候选的 TSF/App 8 小时与 P0 真实宿主同构建矩阵尚未执行，机器 Gate 保持 `NOT_RUN`。
- 浏览器、Win32、WinUI/UWP、登录/凭据等真实输入框仍需人工验收，自动化测试不替代这些运行时边界。

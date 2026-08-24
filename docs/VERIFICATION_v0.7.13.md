# PiInput v0.7.13 验证记录

验证日期：2026-08-23

<!-- release-gates
host_soak_8h=NOT_RUN
tsf_app_soak_8h=NOT_RUN
p0_real_host_matrix=NOT_RUN
-->

## 自动验证

- 当前工作区标准 Release 全量 65/65 CTest 已通过，包含安装/卸载权限边界、实际 PE manifest、卸载临时 worker 和源码完整性回归。
- 已修复新输入框建立 TSF/Host 会话期间的数字吞键：此前 `pending_contexts_` 同时容纳 Resume 同步与真实按键请求，按键门禁把任意 pending 都误判为 composition，导致 Chrome 搜索框可能丢首个数字、远程桌面地址框可能持续不接收数字。当前 `PendingContext` 明确区分请求类型；Resume 不再激活候选数字路径，真实拼音按键在途时仍保留数字选词顺序。源码门禁验证两类请求的标记和消费路径；Chrome/远程桌面同构建人工复测仍属于 P0 真实宿主矩阵。
- 已定位并修复 Windows 搜索无法中文输入：现场模块枚举显示 SearchHost 已加载搜狗 TSF，但未加载 PiInput；机器 profile、`IMMERSIVESUPPORT` 类别和 DLL 的 AppContainer ACL 均存在，缺口是安装器只写 HKCU COM，而打包系统宿主需要 64 位 HKLM COM 才能创建 TIP。提权事务现先把 Shim 部署到 Program Files 的固定受保护路径，再原子注册 profile、capability category 和 HKLM `InprocServer32`；HKCU 指向同一机器 Shim，避免让机器 COM 加载用户可写 DLL。失败恢复此前机器 COM，卸载对称清理注册和机器运行目录。诊断 JSON 与包闭环同时核对 HKCU/HKLM 路径、存在性、哈希和卸载残留。当前候选的 SearchHost 实际加载与最终中文文本仍待安装后验收。
- 专业词结构化回归：59/59；结构化语料总计 313 个用例。
- 大词库：928,725 条；`.lex` 使用只读内存映射，映射字节数由 Host 与 benchmark 对外报告。
- 映射后本机 Host 进程测试：冷启动健康检查 576 ms，首次请求 25 ms，常驻请求 15 ms。
- 前候选 Host-only 8 小时证据：`artifacts/soak-8h-20260823/summary.json` 为 `passed`，运行 2026-08-23 06:12:48Z–14:12:53Z，共 958 个样本；build ID 为 `0.7.13+e3e6c2dc1784-dirty`，词库以 `mmap` 映射 40,758,365 bytes。Private Bytes 从 10,223,616 降至 9,506,816（-716,800；斜率 +35,308.91 bytes/h），Working Set 从 57,978,880 降至 51,445,760（-6,533,120），Handle 从 124 降至 123（-1；斜率 -0.0070/h），全部在阈值内。该运行启动后源码继续变化，只能证明前候选稳定性，不能填写机器 Gate `host_soak_8h=PASS`。
- 历史冻结候选 Host-only 8 小时证据：提交 `a2d5f8fe3c53`、build ID `0.7.13+a2d5f8fe3c53` 的运行已通过，957 个样本及 mmap/Private/WS/Handle 指标均达标。其后安装/卸载实现与构建身份继续变化，因此该证据保留但不关闭下一冻结候选的 `host_soak_8h` Gate。
- Windows 包闭环静态正向与哈希/build ID 失败路径已实跑；脚本覆盖压缩包哈希、精确 commit build ID、RFC3161 签名证据、前版 ZIP 覆盖升级与 UserData 哨兵、当前版安装×2、安装后 Host/TSF 哈希与注册路径、Controlled TSF 实际 DLL、静默卸载和可选重装。正式签名/提权路径尚未执行。
- TSF/App soak harness 的 fixture 正向、低工作负载密度失败、context 重建计数和双进程资源 summary 已验证；它不加载 PiInput，不能填写 `tsf_app_soak_8h=PASS`。
- tag workflow 已 fail-closed：外部 Gate 未全 PASS 不进入构建/签名；PFX secret 仅存在于单一签名步骤，失败路径保留 JUnit 与阶段化 JSON。
- 统一结果聚合器的正向、非法状态、重复 Case ID 和缺失 artifact 路径已通过；CI `always()` 收尾会输出 schema v1 `result.json` 与逐文件 SHA-256 manifest，但在线 workflow 证据尚未产生。
- 冻结候选的未签名开发包已生成，SHA-256 为 `97cf0efadd5247b5c8a16a4ae83b3972eec9559399bd622eff39d5b4d2c1bc9d`，静态身份为 `0.7.13+a2d5f8fe3c53`。安装闭环在 `install-pass-1` 请求管理员权限时被用户取消，阶段化失败证据位于 `artifacts/candidate-a2d5f8fe3c53/package-closure-controlled/summary.json`；它不是产品失败，也不能记为安装闭环 PASS。
- 未经安装器的隔离注册尝试已恢复到原系统状态；Controlled TSF smoke 未在受控宿主加载候选 DLL，`module_identity=false`，证据位于 `artifacts/candidate-a2d5f8fe3c53/controlled-tsf-a2d5f8fe3c53/`。因此 TSF/App Gate 仍为 `NOT_RUN`，不得把后续标点 Oracle 的未执行字段解释为功能失败。
- 外机实测暴露了 `8a8080e` 的权限回归：把安装器整体改为 `asInvoker` 后，文件复制和 HKCU 写入成功，但 `ITfInputProcessorProfileMgr::RegisterProfile` 与 `ITfCategoryMgr::RegisterCategory` 的系统级 TSF 注册在标准令牌下返回 `0x80004005`，造成“文件已复制但无法安装”。当前实现已改为分权事务：原始普通权限进程只处理当前用户文件、HKCU COM、键盘列表和设置；仅 `--machine-register/--machine-unregister` 子步骤通过 `runas` 处理系统级 profile/category。标准账户即使输入另一管理员账户凭据，也不会把用户文件和设置装入管理员账户。
- 卸载仍由普通权限进程处理当前用户键盘项、Host 控制管道、HKCU 和 LocalAppData；提权步骤只反注册系统 TSF，不从临时目录启动 Host、profile 工具或加载产品 DLL。临时 worker 只负责在原用户令牌下删除自身所在安装树，安装/卸载完成与失败弹窗保持前台置顶。

## 尚需正式环境完成

- 本机当前没有可用代码签名证书；未签名候选只能用于开发验证。正式标签流水线必须配置 `PIINPUT_SIGNING_PFX_BASE64` 与 `PIINPUT_SIGNING_PFX_PASSWORD`。
- 冻结候选 `a2d5f8fe3c53` 的 Host-only 8 小时已完成并通过；后续若修改实现或测试代码，必须形成新候选并重新执行，证据文档更新不改变已验证二进制身份。
- 当前候选的 TSF/App 8 小时与 P0 真实宿主同构建矩阵尚未执行，机器 Gate 保持 `NOT_RUN`。
- 正式安装/卸载需要一次窄范围 UAC 授权来写入/删除系统级 TSF profile/category；用户文件、设置和 HKCU 状态不在提权令牌下处理。仍需在标准用户与启用 Windows 应用控制的干净机，用受信任签名的新冻结候选完成闭环。当前自动测试不能替代 UAC 和外机 Smart App Control 验收。
- 浏览器、Win32、WinUI/UWP、登录/凭据等真实输入框仍需人工验收，自动化测试不替代这些运行时边界。

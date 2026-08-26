# PiInput v0.7.15 验证记录

验证日期：2026-08-26

<!-- release-gates
host_soak_8h=PASS
tsf_app_soak_8h=NOT_RUN
p0_real_host_matrix=NOT_RUN
-->

## 冻结候选自动验证

- v0.7.14 功能实现提交 `dd63bb983787` 的 Release 构建已完成 66/66 CTest，静态包闭环通过。
- v0.7.15 统一快捷调用功能的首个冻结提交为 `2326df00048631031cee73a6cca519e2dd5f9fa5`，精确 build ID 为 `0.7.15+2326df000486`；该候选随后被 `bb90009d7aa1c4ab0c56788fdbd82e807afefd57` 取代。
- 该干净提交已在独立目录 `build/windows-x64-v0715-2326df000486` 从零生成 Release 构建，完整 CTest 为 66/66 通过；另一次开发目录全量回归同为 66/66 通过。
- 首个未签名候选现归档于工作区 `releases/candidates/v0.7.15-260826_0106-2326df000486/`；其静态包闭环证据位于 `artifacts/package-closure/history/package-closure-shortcut-table-2326df000486/summary.json`。
- 当前替代候选归档于工作区 `releases/candidates/v0.7.15-260826_0840-bb90009d7aa1/`，ZIP SHA-256 为 `8c9df56420d0ae2b840c83e622543fabb104324258616fadd5d7d99ab707b799`。它的独立 Release 构建为 66/66 通过，GitHub Windows CI 运行 `32878436255` 已通过，未签名 CI 分支的安装、重装和卸载闭环通过；CI 证据位于 `artifacts/ci/history/ci-package-bb90009d7aa1/`。
- 当前替代候选的 Host-only 8 小时稳定性测试已通过，最终证据归档于 `artifacts/soak/validated/soak-8h-final-20260826-bb90009d7aa1/`，`host_soak_8h=PASS`。此结果只覆盖 Host，不替代 TSF/App 8 小时或 P0 真实宿主矩阵。
- 上述静态闭环未运行安装、重装、前版升级、卸载或 Controlled TSF，也未要求正式签名；不得据此宣称安装闭环或正式发布通过。
- 计算器、程序员计算器、画图、统一快捷调用表、撤销的 `kuaijie` 负例、动态行数/图标/旧三槽迁移、英文候选动作、设置往返、Host 协议和同步/异步 TSF edit 时序均已进入自动回归。
- 设置程序内置 95 项 Windows 系统工具模板和 1 项 Everything 模板；模板导入后转换为普通快捷行。YeTool 模板数据的 MIT 许可随包保留，Everything 本体不打包。
- YeSymbol v1.1.1 运行时改为仓库内固定二进制资产，SHA-256、来源提交、MIT 许可证和第三方声明随包发布；干净检出缺少该资产时配置阶段直接失败，不再静默生成缺功能安装包。
- 六份 QA 文档已加入 `IME-EDGE-010..013`、随包 RegCalc 资产和真实启动验收边界。

## 必须完成的发布验证

- 干净 Release 构建与全部 CTest；
- 包内版本、精确 build ID、运行时白名单、ZIP SHA-256 和 RegCalc 三项资产；
- 可信 Authenticode 签名及 RFC 3161 时间戳；
- 当前版安装两次、前版升级、安装后 Host/TSF 路径与哈希、Controlled TSF、卸载与 UserData 保留；
- 同一冻结候选的 Host 8h、TSF/App 8h 和 P0 真实宿主矩阵。

## Host-only 8 小时稳定性结果

- 冻结实现提交：`bb90009d7aa1c4ab0c56788fdbd82e807afefd57`；build ID 精确为 `0.7.15+bb90009d7aa1`。
- 请求时长 8 小时；实际运行 `08:00:06.7654037`，UTC 时间为 2026-08-25 17:39:19.6732737 至 2026-08-26 01:39:26.4386774，共 957 个资源样本，`status=passed`，stderr 为空。
- 词库以 `mmap` 加载，映射字节数为 40,758,365。
- Private Bytes：11,300,864 → 11,055,104，增量 -245,760 bytes，峰值 11,460,608 bytes，斜率 +14,464.55 bytes/hour。
- Working Set：58,970,112 → 55,377,920，增量 -3,592,192 bytes，峰值 59,465,728 bytes。
- Handle：124 → 123，增量 -1，峰值 125，斜率 -0.007008/hour。
- Private、Working Set、Handle 的增量与斜率均在测试门限内；原 `2326df000486` 候选已由本候选正式取代。
- GitHub Windows CI 运行 `32878436255` 已通过，未签名分支的安装、重装和卸载闭环通过。正式签名仍为 N/A，Controlled TSF 仍为 BLOCKED。

## 当前外部条件

- 本机证书存储没有适用于公开发行的可信代码签名证书；GitHub 仓库当前也未配置 `PIINPUT_SIGNING_PFX_BASE64` 与 `PIINPUT_SIGNING_PFX_PASSWORD`。
- GitHub 托管 Windows runner 可以验证构建、测试、安装、重装和卸载闭环，但没有受支持的交互式输入桌面。Controlled TSF 物理按键测试必须单列为 `BLOCKED`，不得把“窗口未加载 TSF”的全 false 结果误报为功能失败，也不得伪造为通过。
- 自动化不会打开真实桌面宿主或抢占用户鼠标。需要人工操作的 P0 宿主项目在完成前保持 `NOT_RUN`。

在三个机器 Gate 都为 `PASS`、正式签名可用并且包闭环通过之前，本文件不得作为正式发布通过证明。
